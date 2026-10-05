#include <std_include.hpp>
#include "weapon_render_pose_cache.hpp"
#include "viewmodel_visibility.hpp"
#include "scripted_body.hpp"
#include "vehicle_runtime.hpp"
#include "vehicle_hud.hpp"
#include "empty_hands_native.hpp"
#include "hand_attachment_pose.hpp"
#include "mounted_turret.hpp"
#include "weapon_carry_runtime.hpp"
#include "../eye_composition.hpp"
#include <utils/native_memory.hpp>
#include "loader/component_loader.hpp"
#include "loader/target_identity.hpp"
#include "component/console.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapon_render_pose
{
	namespace
	{
		// SkinSceneDObj is called after the native ownership CAS (2 -> 3).
		// AddDObjSurfaces consumes that entity's +0x68 surface buffer for a
		// specific record, before native scene-surface completion is signaled.
		constexpr std::uintptr_t skin_site = 0x14071F167, skin_target = 0x14071F190;
		constexpr std::uintptr_t submit_site = 0x14071FF74, submit_target = 0x140774F10;
		constexpr std::uintptr_t viewmodels_site = 0x140779439, viewmodels_target = 0x14077F690;
		using viewmodels_fn = void(*)(void*,void*,int);
		thread_local void* viewmodel_record{};
		void viewmodels_stub(void* view, void* record, int mode)
		{
			const auto before = viewmodel_record;
			const auto restore = gsl::finally([&] { viewmodel_record = before; });
			viewmodel_record = record;
			hands::attachments::begin_record(record);
			// Native builds/skins its transient viewmodel range here, BEFORE rigid
			// scene preparation. AddDObjSurfaces is a later separate boundary.
			reinterpret_cast<viewmodels_fn>(viewmodels_target)(view,record,mode);
		}
		using skin_fn = int(*)(void*, void*, const hands::bone*);
		using submit_fn = std::uintptr_t(*)(void*, void*, void*, std::uint32_t,
			std::uint32_t, void*, void*, std::uint32_t);
		std::mutex mutex;
		pose_cache cache;
		std::atomic_bool alive{true};
		std::atomic<std::uintptr_t> primary_object{};
		std::atomic_uint64_t solves{}, skin_calls{}, skin_matches{}, skin_misses{}, submissions{}, binds{}, misses{};
		std::atomic_uint64_t selections{}, selection_misses{}, registrations{};
		std::atomic_uint64_t preparations{}, preparation_misses{};
		void register_render_scene(const engine_stereo_view::slot_pair& views, std::uintptr_t record) noexcept
		{
			vehicles::begin_scene(views,record);
			const auto reference=controller_input::latest().reference_generation;
			const auto primary=weapons::current_hold();
			if (weapons::carry::active()) for (const auto& held:weapons::carry::held_instances())
				if (held.id && held.owner.id()!=primary.id()) begin_scene(views,record,held.owner,reference);
			// Shared by optics, HUD and attachment presentation in every build.
			begin_scene(views,record,primary,reference);
		}
		template<class T> bool read(const void* base, std::size_t offset, T& output) noexcept
		{
			const auto address = reinterpret_cast<std::uintptr_t>(base);
			if (!address || address > (std::numeric_limits<std::uintptr_t>::max)() - offset) return false;
			return utils::native_memory::read_bytes(&output,
				reinterpret_cast<const void*>(address + offset), sizeof(output));
		}
		int skin_stub(void* entity, void* object, const hands::bone* matrices)
		{
			mounted::prepare_skin(object,matrices);
			const auto attached=hands::attachments::before_skin(object,matrices);
			const auto native=[&] {
				const auto result=reinterpret_cast<skin_fn>(skin_target)(entity,object,matrices);
				hands::attachments::after_skin(attached,result,viewmodel_record);return result;
			};
			const auto previous=weapons::viewmodel_visibility::skin_object(object);
			sequences::body::apply(object,matrices);
			vehicles::mask_native(object,matrices);
			const auto restore=gsl::finally([&] { weapons::viewmodel_visibility::skin_object(previous); });
			const auto obj = reinterpret_cast<std::uintptr_t>(object);
			// Stored skeletons have no firing/hand witness. In particular, an
			// object that was held earlier must not bind its previous muzzle epoch.
			if(hands::owned_native::owns(object) && !hands::owned_native::owner(object).id())return native();
			if (!alive.load() || !obj || (obj != primary_object.load() && !hands::owned_native::owns(object)))
				return native();
			++skin_calls;
			solved_pose pose;
			std::uint32_t epoch{};
			bool matched = read(object, 0xB0, epoch);
			{ const std::lock_guard lock(mutex);
				cache.invalidate_skin(reinterpret_cast<std::uintptr_t>(entity));
				matched = matched && cache.find(obj, reinterpret_cast<std::uintptr_t>(matrices), epoch, pose); }
			hands::bone before{}, after{},laser_before{},laser_after{};
			matched = matched && read(matrices, pose.muzzle_bone * sizeof(hands::bone), before) &&
				std::memcmp(&before, &pose.bone, sizeof(before)) == 0;
			if(matched && pose.muzzle.laser.valid)matched=pose.laser_bone<256 &&
				read(matrices,pose.laser_bone*sizeof(hands::bone),laser_before) && std::memcmp(&laser_before,&pose.laser,sizeof(laser_before))==0;
			// No MOD lock across native work. Always forward exactly once.
			const auto result = native();
			std::uintptr_t surface{};
			matched = matched && result > 0 && read(entity, 0x68, surface) && surface &&
				read(matrices, pose.muzzle_bone * sizeof(hands::bone), after) &&
				std::memcmp(&before, &after, sizeof(before)) == 0;
			if(matched && pose.muzzle.laser.valid)matched=read(matrices,pose.laser_bone*sizeof(hands::bone),laser_after) &&
				std::memcmp(&laser_before,&laser_after,sizeof(laser_before))==0;
			if (matched)
			{
				std::array<float,12> camera{};
				const bool has_record = viewmodel_record && read(viewmodel_record,engine_stereo_view::h2_view_origin_offset,camera);
				const std::lock_guard lock(mutex);
				matched = cache.skin(reinterpret_cast<std::uintptr_t>(entity), surface, pose, before,pose.muzzle.laser.valid?&laser_after:nullptr);
				if (matched && has_record)
				{
					if (cache.prepare(reinterpret_cast<std::uintptr_t>(viewmodel_record),camera,
						reinterpret_cast<std::uintptr_t>(entity),surface,obj)) ++preparations;
					else ++preparation_misses;
				}
			}
			if (matched) ++skin_matches; else ++skin_misses;
			return result;
		}
		std::uintptr_t submit_stub(void* record, void* parameters, void* entity, std::uint32_t mask,
			std::uint32_t flags, void* starts, void* ends, std::uint32_t distance)
		{
			std::uintptr_t object{}, surface{};
			std::array<float, 12> camera{};
			const bool selected = alive.load() && read(entity, 0x98, object) && object &&
				(!hands::owned_native::owns(reinterpret_cast<const void*>(object)) || hands::owned_native::owner(reinterpret_cast<const void*>(object)).id()) &&
				(object == primary_object.load() || hands::owned_native::owns(reinterpret_cast<const void*>(object))) && read(entity, 0x68, surface) && surface &&
				read(record, engine_stereo_view::h2_view_origin_offset, camera);
			const auto result = reinterpret_cast<submit_fn>(submit_target)(record, parameters, entity,
				mask, flags, starts, ends, distance);
			if (selected)
			{
				++submissions;
				const std::lock_guard lock(mutex);
				if (cache.submit(reinterpret_cast<std::uintptr_t>(record), camera,
					reinterpret_cast<std::uintptr_t>(entity), surface, object)) ++binds;
				else ++misses;
			}
			return result;
		}
	}
	void publish_solved(const solved_pose& pose) noexcept
	{
		if (!alive.load()) return;
		const std::lock_guard lock(mutex);
		if (cache.publish(pose)) { primary_object = pose.object; ++solves; }
	}
	void begin_scene(const engine_stereo_view::slot_pair& views, std::uintptr_t record,
		const weapons::hold& owner, std::uint64_t reference) noexcept
	{
		if (!alive.load()) return;
		const std::lock_guard lock(mutex);
		if (cache.begin(views, record, owner, reference)) ++registrations;
	}
	bool for_scene(const engine_stereo_view::slot_pair& views, snapshot& output, weapons::weapon_identity weapon) noexcept
	{
		const std::lock_guard lock(mutex);
		if (alive.load() && cache.get(views, output,weapon)) { ++selections; return true; }
		++selection_misses; return false;
	}
	std::string status()
	{
		return std::format("render_pose=native_skin_submission solves={} skin_calls={} skin_matches={} skin_misses={} submissions={} binds={} bind_misses={} registrations={} selections={} selection_misses={} viewmodel_preparations={} preparation_misses={}\n",
			solves.load(), skin_calls.load(), skin_matches.load(), skin_misses.load(), submissions.load(),
			binds.load(), misses.load(), registrations.load(), selections.load(), selection_misses.load(),preparations.load(),preparation_misses.load());
	}
	bool for_record(std::uintptr_t record, const std::array<float,12>& camera, snapshot& output, weapons::weapon_identity weapon) noexcept
	{
		const std::lock_guard lock(mutex);
		return alive.load() && cache.for_record(record,camera,output,weapon);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			if (!target_identity::get().compatibility_probe_passed)
				throw std::runtime_error("weapon render pose requires verified H2 image");
			const std::array<std::uintptr_t, 3> sites{skin_site, submit_site, viewmodels_site}, targets{skin_target, submit_target, viewmodels_target};
			const std::array<void*, 3> stubs{reinterpret_cast<void*>(skin_stub), reinterpret_cast<void*>(submit_stub), reinterpret_cast<void*>(viewmodels_stub)};
			for (unsigned i = 0; i < sites.size(); ++i)
			{
				std::array<std::uint8_t, 5> bytes{0xE8}, mask{0xFF,0xFF,0xFF,0xFF,0xFF};
				const auto displacement = static_cast<std::int32_t>(targets[i] - sites[i] - 5);
				std::memcpy(bytes.data()+1, &displacement, sizeof(displacement));
				if (!utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(targets[i])) ||
					!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(sites[i]), {bytes.data(),mask.data(),bytes.size()}) ||
					utils::hook::is_relatively_far(reinterpret_cast<void*>(sites[i]), stubs[i]))
					throw std::runtime_error("weapon render pose call-site validation failed");
			}
			for (unsigned i = 0; i < sites.size(); ++i) utils::hook::call(sites[i], stubs[i]);
			eye_composition::scene_record_consumer.store(register_render_scene);
			console::info("[VR HUD] native skin/submission pose binding installed\n");
		}
		void pre_destroy() override { alive = false;eye_composition::scene_record_consumer.store(nullptr); }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapon_render_pose::component)
