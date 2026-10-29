#include <std_include.hpp>
#include "underbarrel_presenter.hpp"
#include "underbarrel_clearance.hpp"
#include "weapon_render_pose.hpp"
#include "viewmodel_visibility.hpp"
#include "component/scene_model_record.hpp"
#include "component/scene_pose_match.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scene_models.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>

namespace vr::gameplay::weapons::underbarrel::round_presentation
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		struct asset
		{
			scene_models::rigid_part round, casing;
			game::XModel* source{};
			game::XSurface* surfaces{};
			unsigned variant{};
		};
		std::array<std::unique_ptr<asset>, 16> retained;
		std::array<std::atomic<asset*>, 2> available{};
		struct measured_geometry
		{
			game::XModel* source{};
			game::XSurface* surfaces{};
			float clearance{};
		};
		std::array<measured_geometry, 2> measured{};
		std::array<std::atomic<float>, 2> clearances{};
		std::array<std::atomic<const char*>, 2> admission{
		    {"waiting for loaded attachment", "waiting for loaded attachment"}};
		std::atomic_bool alive{true};
		std::mutex mutex;
		struct frame
		{
			hold owner{};
			asset* mesh{};
			std::uintptr_t object{}, matrices{};
			std::uint32_t epoch{};
			std::uint64_t reference{};
			clock::time_point at{};
			anchor local{}, world{};
			chamber_visual content{};
			float units{};
		};
		std::array<frame, 15> latest{};
		std::array<frame, 128> poses{};
		size_t cursor{};
		std::array<unsigned short, 15> lighting{};
		std::array<extraction, 16> pending{};
		size_t pending_count{};
		struct drop
		{
			asset* mesh{};
			extraction event{};
		};
		std::array<drop, 16> drops{};
		size_t drop_cursor{};
		std::uint64_t requested{}, released{}, overflow{};

		anchor bind(const scene_models::rigid_part& part)
		{
			const auto b = part.bind();
			return {{b[0], b[1], b[2]}, normalize({b[3], b[4], b[5], b[6]})};
		}
		scene_models::rigid_part& geometry(const frame& f)
		{
			return f.content == chamber_visual::spent ? f.mesh->casing : f.mesh->round;
		}
		void refresh()
		{
			if (!alive || !game::CL_IsCgameInitialized())
				return;
			for (unsigned variant = 0; variant < m203_models.size(); ++variant)
			{
				auto* source =
				    game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, m203_models[variant], 0).model;
				if (!source || !source->name || std::string_view(source->name) != m203_models[variant] ||
				    source->numBones != m203_bones.size() || source->numRootBones != 1 ||
				    !source->boneNames || !source->parentList)
				{
					available[variant] = nullptr;
					clearances[variant] = 0;
					measured[variant] = {};
					admission[variant] = "loaded attachment identity unavailable";
					continue;
				}
				bool topology = true;
				for (unsigned b = 0; b < m203_bones.size(); ++b)
				{
					const auto* name = game::SL_ConvertToString(source->boneNames[b]);
					if (!name || std::string_view(name) != m203_bones[b] ||
					    (b && int(b) - source->parentList[b - 1] != m203_parents[b]))
						topology = false;
				}
				if (!topology)
				{
					available[variant] = nullptr;
					clearances[variant] = 0;
					measured[variant] = {};
					admission[variant] = "attachment topology rejected";
					continue;
				}
				auto& measurement = measured[variant];
				if (measurement.source != source || measurement.surfaces != source->lodInfo[0].surfs ||
				    measurement.clearance <= 0)
					measurement = {source, source->lodInfo[0].surfs, measure_case_clearance(source)};
				clearances[variant] = measurement.clearance;
				// The mechanical clearance is CPU metadata; it does not wait for
				// cosmetic model construction or GPU/scene admission.
				if (!scene_models::ready() || !viewmodel_visibility::ready(part_visibility::rigid_groups))
					continue;
				asset* found{};
				for (auto& entry : retained)
					if (entry && entry->source == source && entry->surfaces == source->lodInfo[0].surfs &&
					    entry->variant == variant)
					{
						found = entry.get();
						break;
					}
				if (found)
				{
					available[variant] = found;
					admission[variant] = "ready";
					continue;
				}
				available[variant] = nullptr;
				admission[variant] = "retained model capacity reached";
				for (auto& entry : retained)
					if (!entry)
					{
						auto next = std::make_unique<asset>();
						const std::array<unsigned, 2> round_bones{5, 6};
						if (!next->round.create(source, 5, round_bones))
						{
							admission[variant] = next->round.status();
							break;
						}
						if (!next->casing.create(source, 6))
						{
							admission[variant] = next->casing.status();
							break;
						}
						const std::array<scene_models::runtime_model, 2> identities{
						    {{next->round.model(), source}, {next->casing.model(), source}}};
						if (!scene_models::register_runtime_models(identities))
						{
							admission[variant] = "model registration rejected";
							break;
						}
						next->source = source;
						next->surfaces = source->lodInfo[0].surfs;
						next->variant = variant;
						entry = std::move(next);
						available[variant] = entry.get();
						admission[variant] = "ready";
						break;
					}
			}
		}
		template <class T> bool read(const void* p, size_t offset, T& value) noexcept
		{
			return p && utils::native_memory::read_bytes(
			                &value, static_cast<const std::byte*>(p) + offset, sizeof(value));
		}
		bool current_frame(const frame& f, clock::time_point now)
		{
			if (!f.mesh || f.mesh != available[f.mesh->variant].load() ||
			    f.content == chamber_visual::empty || now < f.at || now - f.at > 150ms)
				return false;
			const auto v = current(f.owner.id());
			return v.active && !v.fault && v.reference == f.reference &&
			       v.owner.rear_revision == f.owner.rear_revision && visible_chamber(v.ammo) == f.content &&
			       carry::held(f.owner.id()).can_fire();
		}
		scene_models::placement_result prepare(const scene_models::preparation& preparing,
		                                       const void* entry,
		                                       game::GfxPlacement& placed,
		                                       game::GfxPlacement& previous) noexcept
		{
			using result = scene_models::placement_result;
			std::uintptr_t handle{};
			const auto base = reinterpret_cast<std::uintptr_t>(lighting.data());
			if (!scene_models::native_entry::lighting(entry, handle) || handle < base ||
			    handle >= base + sizeof(lighting) || (handle - base) % sizeof(unsigned short))
				return result::unchanged;
			weapon_identity id;
			{
				const std::lock_guard lock(mutex);
				id = latest[(handle - base) / sizeof(unsigned short)].owner.id();
			}
			std::array<float, 12> camera{};
			weapon_render_pose::snapshot skin;
			if (!alive || !read(preparing.record, engine_stereo_view::h2_view_origin_offset, camera) ||
			    !weapon_render_pose::for_record(
			        reinterpret_cast<std::uintptr_t>(preparing.record), camera, skin, id))
				return result::omit;
			frame f;
			bool found{};
			{
				const std::lock_guard lock(mutex);
				found = scene_models::latest_skeleton_pose(
				    poses,
				    cursor,
				    skin,
				    [&](const frame& value) noexcept { return value.owner.id() == id; },
				    f);
			}
			game::XModel* submitted{};
			vec origin{};
			if (!found || !current_frame(f, clock::now()) ||
			    !scene_models::native_entry::model(entry, submitted) || submitted != geometry(f).model() ||
			    !read(preparing.record, engine_stereo_view::h2_current_model_placement_origin_offset, origin))
				return result::omit;
			for (float x : origin)
				if (!std::isfinite(x) || std::abs(x) > 1e7f)
					return result::omit;
			auto pose = f.local;
			pose.position = add(pose.position, origin);
			pose = compose(pose, inverse(bind(geometry(f))));
			std::copy(pose.position.begin(), pose.position.end(), placed.origin);
			std::copy(pose.rotation.begin(), pose.rotation.end(), placed.quat);
			previous = placed;
			return result::replace;
		}
		void submit()
		{
			if (!alive || !game::CL_IsCgameInitialized())
				return;
			const auto now = clock::now();
			const auto input = controller_input::latest();
			std::array<frame, 15> frames;
			std::array<drop, 16> released_cases;
			{
				const std::lock_guard lock(mutex);
				size_t kept{};
				for (size_t i = 0; i < pending_count; ++i)
				{
					const auto& e = pending[i];
					auto* mesh = available[unsigned(e.digital)].load();
					if (e.reference != input.reference_generation || !case_flight(e).path.alive(now))
						continue;
					if (mesh)
					{
						drops[drop_cursor++ % drops.size()] = {mesh, e};
						++released;
					}
					else
						pending[kept++] = e; // A first-frame shot may precede main-thread asset preparation.
				}
				pending_count = kept;
				frames = latest;
				released_cases = drops;
			}
			if (!input.focused)
				return;
			for (unsigned i = 0; i < frames.size(); ++i)
			{
				const auto& f = frames[i];
				if (f.reference != input.reference_generation || !current_frame(f, now))
					continue;
				auto& model = geometry(f);
				const auto pose = compose(f.world, inverse(bind(model)));
				game::GfxScaledPlacement place{};
				place.scale = pose.scale;
				std::copy(pose.position.begin(), pose.position.end(), place.base.origin);
				std::copy(pose.rotation.begin(), pose.rotation.end(), place.base.quat);
				float color[]{1, 1, 1, 1};
				scene_models::submit(
				    model.model(), &place, 0, &lighting[i], color, color, color, .2f * f.units);
			}
			for (const auto& d : released_cases)
			{
				if (!d.mesh || d.event.reference != input.reference_generation)
					continue;
				auto flight = case_flight(d.event);
				if (!flight.path.alive(now))
					continue;
				flight.local = inverse(bind(d.mesh->casing));
				falling_item_presentation::submit(
				    d.mesh->casing.model(), flight, d.event.reference, .2f * d.event.units);
			}
		}
	}
	float case_clearance_units(bool digital) noexcept
	{
		return clearances[unsigned(digital)].load();
	}
	void eject(const extraction& event) noexcept
	{
		if (!alive || !scheduler::is_executing(scheduler::pipeline::server) || !event.owner ||
		    !event.reference || !case_flight(event).path.valid())
			return;
		const std::lock_guard lock(mutex);
		++requested;
		if (pending_count == pending.size())
		{
			++overflow;
			return;
		}
		pending[pending_count++] = event;
	}
	void present(void* object,
	             std::uint32_t epoch,
	             const void* matrices,
	             const part_rig& parts,
	             const presentation& v,
	             const hold& owner,
	             const controller_input::frame& input,
	             anchor gun,
	             vec offset,
	             float units,
	             bool gameplay) noexcept
	{
		if (!alive || !parts || parts.type != kind::m203 || !owner.id() || !std::isfinite(units) ||
		    units <= 0)
			return;
		const auto content = v.active && !v.fault ? visible_chamber(v.ammo) : chamber_visual::empty;
		auto local = compose(gun, chamber_pose(parts, content, v.travel, units));
		if (content != chamber_visual::empty)
			local = compose(local, parts.round_parts_local[content == chamber_visual::spent ? 1 : 0]);
		auto world = local;
		world.position = add(world.position, offset);
		frame f{owner,
		        available[unsigned(parts.digital)].load(),
		        reinterpret_cast<std::uintptr_t>(object),
		        reinterpret_cast<std::uintptr_t>(matrices),
		        epoch,
		        input.reference_generation,
		        input.sampled_at,
		        local,
		        world,
		        gameplay ? content : chamber_visual::empty,
		        units};
		const std::lock_guard lock(mutex);
		auto* slot = &latest[0];
		for (auto& entry : latest)
			if (entry.owner.id() == owner.id())
			{
				slot = &entry;
				break;
			}
			else if (!entry.mesh || entry.at < slot->at)
				slot = &entry;
		*slot = f;
		poses[cursor++ % poses.size()] = f;
	}
	std::string status()
	{
		const std::lock_guard lock(mutex);
		return std::format(
		    "m203_rounds base={} digital={} case_requests={} released={} overflow={} clearance_units={}/{}\n",
		    admission[0].load(),
		    admission[1].load(),
		    requested,
		    released,
		    overflow,
		    clearances[0].load(),
		    clearances[1].load());
	}
	class presenter_component final : public component_interface
	{
		void post_unpack() override
		{
			scene_models::on_submit(submit);
			scene_models::on_prepare_placement(prepare);
			scheduler::loop(refresh, scheduler::pipeline::main, 250ms);
			fastfiles::on_pre_unload(
			    []
			    {
				    for (auto& p : available)
					    p = nullptr;
				    for (auto& value : clearances)
					    value = 0;
				    measured = {};
				    {
					    const std::lock_guard lock(mutex);
					    latest = {};
					    poses = {};
					    pending = {};
					    drops = {};
					    cursor = drop_cursor = pending_count = 0;
				    }
				    for (auto& p : retained)
					    p.reset();
				    for (auto& value : admission)
					    value = "waiting for loaded attachment";
			    });
		}
		void pre_destroy() override
		{
			alive = false;
			for (auto& p : available)
				p = nullptr;
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::underbarrel::round_presentation::presenter_component)
