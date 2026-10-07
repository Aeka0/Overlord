#include <std_include.hpp>
#include "component/scene_model_record.hpp"
#include "runtime.hpp"
#include "model_pose.hpp"
#include "effect.hpp"
#include "../hands/position_offset.hpp"
#include "../native_weapon_fx.hpp"
#include "../hands/attachment_pose.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include "../viewmodel_visibility.hpp"
#include "../../engine_stereo_view.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::vehicles
{
	namespace
	{
		using namespace hands::pose_math;
		struct part
		{
			std::unique_ptr<scene_models::rigid_part> mesh;
			anchor local{};
			bool magazine{}, round{};
		};
		struct resource
		{
			kind type{};
			game::XModel* source{};
			std::vector<part> parts;
			vec center{};
			anchor muzzle{}, brass{};
			bool has_brass{};
			game::FxEffectDef *flash_fx{}, *brass_fx{};
		};
		std::array<std::shared_ptr<resource>, 2> resources;
		std::atomic_uint asset_mask{};
		std::mutex mutex;
		// Stable submission slots retain one immutable state for every part.
		// Backend preparation can overlap a newer frontend/server frame.
		struct submission
		{
			std::shared_ptr<resource> asset;
			snapshot state;
			std::array<unsigned short, 64> lighting{};
			std::uint64_t serial{};
		};
		std::array<submission, 128> submissions{};
		std::uint64_t submission_serial{};
		struct placement_frame
		{
			const void* record{};
			std::array<float, 12> camera{};
			std::uint64_t serial{};
			anchor gun{}, magazine{};
			bool valid{}, retain{};
		};
		std::array<placement_frame, 128> placements{};
		size_t placement_cursor{};
		std::atomic<const void*> masked_object{};
		std::array<game::XModel*, 2> attempted{};
		rendered_gun rendered;
		std::uint64_t render_serial{};
		std::array<effect_request, 16> effects{};
		size_t effect_count{};
		std::atomic_uint64_t fx_emitted{}, fx_rejected{};
		template <class T> bool read(const void* p, size_t offset, T& out)
		{
			return p && utils::native_memory::read_bytes(
			                &out, static_cast<const std::byte*>(p) + offset, sizeof(out));
		}
		anchor bind(const game::DObjAnimMat& b)
		{
			return {{b.trans[0], b.trans[1], b.trans[2]},
			        normalize({b.quat[0], b.quat[1], b.quat[2], b.quat[3]})};
		}
		bool fresh(const snapshot& s)
		{
			const auto now = controller_input::clock::now();
			const auto c = current();
			return presentation_allowed() && c.epoch == s.driving.epoch && c.entity == s.driving.entity &&
			       s.reference && now >= s.at && now - s.at <= 150ms;
		}
		void prepare_assets()
		{
			const auto c = current();
			if (!c || !active() || !native::ready(c.type) || !scene_models::ready())
				return;
			const auto index = c.type == kind::zodiac ? 0 : 1;
			{
				const std::lock_guard lock(mutex);
				if (resources[index])
					return;
			}
			auto* model = game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, receiver_name(c.type), 0).model;
			if (!model || !model->name || std::string_view(model->name) != receiver_name(c.type) ||
			    !model->numBones || model->numBones > 64 || !model->baseMat || !model->boneNames ||
			    !model->numLods)
				return;
			if (attempted[index] == model)
				return;
			attempted[index] = model;
			const auto& p = profile(c.type);
			const auto& reload = *p.reload;
			auto result = std::make_shared<resource>();
			result->type = c.type;
			result->source = model;
			result->center = {
			    model->bounds.midPoint[0], model->bounds.midPoint[1], model->bounds.midPoint[2]};
			std::array<std::string_view, 64> names{};
			std::array<bone_definition, 64> bones{};
			std::array<anchor, 64> rest{};
			int magazine = -1, muzzle = -1, brass = -1;
			if (model->numRootBones != 1 || !model->parentList)
				return;
			for (unsigned i = 0; i < model->numBones; ++i)
			{
				const auto* name = game::SL_ConvertToString(model->boneNames[i]);
				if (!name)
					return;
				names[i] = name;
				bones[i].name = name;
				bones[i].parent = i ? int(i) - model->parentList[i - 1] : -1;
				const auto source = bind(model->baseMat[i]);
				bones[i].bind.position = source.position;
				bones[i].bind.rotation = source.rotation;
				if (names[i] == reload.magazine_bone)
					magazine = int(i);
				if (names[i] == "tag_flash")
					muzzle = int(i);
				if (names[i] == "tag_brass")
					brass = int(i);
			}
			if (magazine < 0 || muzzle < 0 ||
			    !model_rest_pose({bones.data(), model->numBones}, p.equip_rest, rest))
				return;
			result->center =
			    compose(inverse(bind(model->baseMat[0])), {result->center, {0, 0, 0, 1}}).position;
			result->muzzle = rest[muzzle];
			if (brass >= 0)
			{
				result->brass = rest[brass];
				result->has_brass = true;
			}
			const auto effect = [](const char* name)
			{
				auto* fx = game::DB_FindXAssetHeader(game::ASSET_TYPE_FX, name, 0).fx;
				return fx && fx->name && std::string_view(fx->name) == name ? fx : nullptr;
			};
			// Both native driver scripts load these exact view FX assets.
			result->flash_fx = effect("fx/muzzleflashes/uzi_flash_view");
			result->brass_fx = effect("vfx/shelleject/pistol_view");
			std::array<bool, 64> magazine_bones{};
			magazine_bones[magazine] = true;
			for (unsigned i = unsigned(magazine) + 1; i < model->numBones; ++i)
			{
				if (!model->parentList || !model->parentList[i - 1] || model->parentList[i - 1] > i)
					return;
				magazine_bones[i] =
				    magazine_bones[i - model->parentList[i - 1]] || names[i] == reload.bullets_bone;
			}
			for (unsigned i = 0; i < model->numBones; ++i)
				if (names[i] == reload.bullets_bone)
					magazine_bones[i] = true;
			std::vector<scene_models::runtime_model> identities;
			for (unsigned i = 0; i < model->numBones; ++i)
			{
				// The source models contain optional sights even on these bare guns.
				if (names[i] == "tag_acog_2" || names[i] == "tag_rail" || names[i] == "tag_red_dot" ||
				    names[i] == "tag_eotech" || names[i] == "tag_silencer" || names[i] == "tag_thermal_scope")
					continue;
				auto mesh = std::make_unique<scene_models::rigid_part>();
				if (!mesh->create(model, i))
					continue;
				auto local = rigid_delta(rest[i], bind(model->baseMat[i]));
				// Deliberately static action pose during driving: no empty slide lock,
				// no open-bolt home/cocking presentation. Magazine parts remain separate.
				if (magazine_bones[i])
					local = compose(inverse(reload.magazine_rest), local);
				identities.push_back({mesh->model(), model});
				result->parts.push_back(
				    {std::move(mesh), local, magazine_bones[i], names[i] == reload.bullets_bone});
			}
			if (result->parts.empty() || result->parts.size() > 32 ||
			    !scene_models::register_runtime_models(identities))
				return;
			const std::lock_guard lock(mutex);
			resources[index] = std::move(result);
			asset_mask.fetch_or(1u << unsigned(c.type));
		}
		anchor root_pose(const snapshot& s, const head_pose_bridge::spatial_frame& body, const resource& r)
		{
			if (s.owner.can_fire())
				return s.gun;
			auto out = chest(body);
			out.position = sub(out.position, rotate(out.rotation, r.center));
			return out;
		}
		void submit()
		{
			const auto s = latest();
			if (!fresh(s))
				return;
			std::shared_ptr<resource> r;
			submission* batch{};
			{
				const std::lock_guard lock(mutex);
				r = resources[s.driving.type == kind::zodiac ? 0 : 1];
				batch = &submissions[submission_serial % submissions.size()];
				batch->asset = r;
				batch->state = s;
				batch->serial = ++submission_serial;
			}
			if (!r)
				return;
			// Emit on the native frontend, using the same solved wrist/root as
			// the gun. Script/server positions can trail a fast boat by metres.
			std::array<effect_request, 16> pending{};
			size_t count{};
			rendered_gun gun;
			{
				const std::lock_guard lock(mutex);
				gun = rendered;
				const auto now = controller_input::clock::now();
				size_t keep{};
				for (size_t i = 0; i < effect_count; ++i)
				{
					const auto& e = effects[i];
					switch (effect_state(e, s.owner, s.reference, gun, now))
					{
					case effect_readiness::reject:
						++fx_rejected;
						break;
					case effect_readiness::ready:
						pending[count++] = e;
						break;
					case effect_readiness::wait:
						effects[keep++] = e;
						break;
					}
				}
				effect_count = keep;
			}
			for (size_t i = 0; i < count; ++i)
			{
				const bool brass = pending[i].brass;
				const auto at = compose(gun.pose, brass ? r->brass : r->muzzle);
				if ((!brass || r->has_brass) &&
				    (brass ? weapons::native_weapon_fx::play_shell_frontend(r->brass_fx, at) :
				             weapons::native_weapon_fx::play_frontend(r->flash_fx, at)))
					++fx_emitted;
				else
					++fx_rejected;
			}
			head_pose_bridge::spatial_frame body;
			if (!head_pose_bridge::get_spatial_frame(body) || body.generation != s.reference)
				return;
			auto root = root_pose(s, body, *r);
			if (s.owner.can_fire() && gun.owner.id() == s.owner.id() &&
			    gun.owner.rear_revision == s.owner.rear_revision && gun.reference == s.reference &&
			    controller_input::clock::now() >= gun.at && controller_input::clock::now() - gun.at <= 150ms)
				root = gun.pose;
			const auto& reload = *profile(s.driving.type).reload;
			for (size_t i = 0; i < r->parts.size(); ++i)
			{
				const auto& p = r->parts[i];
				const auto draw = [&](anchor base, size_t slot)
				{
					const auto world = compose(base, p.local);
					game::GfxScaledPlacement at{};
					at.scale = 1;
					std::copy(world.position.begin(), world.position.end(), at.base.origin);
					std::copy(world.rotation.begin(), world.rotation.end(), at.base.quat);
					float color[4]{1, 1, 1, 1};
					scene_models::submit(
					    p.mesh->model(), &at, 0, &batch->lighting[slot], color, color, color, 8.f);
				};
				if (!p.magazine)
					draw(root, i);
				else
				{
					if (s.inserted && (!p.round || s.rounds > 0))
					{
						auto at = compose(root, reload.magazine_rest);
						if (s.magazine_grabbed)
							at.position =
							    add(at.position,
							        rotate(root.rotation,
							               rotate(reload.well.rotation, {0, 0, -s.pull * s.units})));
						draw(at, i);
					}
					if (vr::valid_hand(s.magazine_hand) && !s.magazine_grabbed &&
					    (!p.round || s.held_rounds > 0))
						draw(s.magazine, i + 32);
				}
			}
		}
		scene_models::placement_result place(const scene_models::preparation& p,
		                                     const void* entry,
		                                     game::GfxPlacement& placed,
		                                     game::GfxPlacement& previous) noexcept
		{
			using result = scene_models::placement_result;
			std::uintptr_t handle{};
			if (!scene_models::native_entry::lighting(entry, handle))
				return result::unchanged;
			const auto first = reinterpret_cast<std::uintptr_t>(submissions.data());
			if (handle < first || handle >= first + sizeof(submissions))
				return result::unchanged;
			const auto batch_index = (handle - first) / sizeof(submission);
			std::array<float, 12> camera{};
			if (!read(p.record, engine_stereo_view::h2_view_origin_offset, camera))
				return result::omit;
			const std::lock_guard lock(mutex);
			const auto& batch = submissions[batch_index];
			const auto handles = reinterpret_cast<std::uintptr_t>(batch.lighting.data());
			if (handle < handles || handle >= handles + sizeof(batch.lighting) ||
			    (handle - handles) % sizeof(unsigned short))
				return result::unchanged;
			const auto slot = (handle - handles) / sizeof(unsigned short), index = slot % 32;
			const bool detached = slot >= 32;
			const auto& s = batch.state;
			const auto& r = batch.asset;
			if (!r || index >= r->parts.size())
				return result::omit;
			const auto& part = r->parts[index];
			game::XModel* model{};
			if (!scene_models::native_entry::model(entry, model) || model != part.mesh->model())
				return result::omit;
			placement_frame* frame{};
			for (auto& candidate : placements)
				if (candidate.record == p.record && candidate.serial == batch.serial &&
				    candidate.camera == camera)
				{
					frame = &candidate;
					break;
				}
			if (!frame)
			{
				frame = &placements[placement_cursor++ % placements.size()];
				*frame = {};
				frame->record = p.record;
				frame->camera = camera;
				frame->serial = batch.serial;
				// Resolve visibility and attachment ONCE for the whole scene, never
				// reread a changing owner/ammo/clock independently for each mesh.
				const auto live = latest();
				vec origin{};
				hands::attachments::solved solved;
				if (fresh(s) && live.owner.id() == s.owner.id() &&
				    live.owner.rear_revision == s.owner.rear_revision && live.reference == s.reference)
				{
					if (hands::attachments::for_record(p.record, camera, solved) &&
					    solved.reference == s.reference &&
					    read(p.record, engine_stereo_view::h2_current_model_placement_origin_offset, origin))
					{
						if (s.owner.can_fire())
						{
							const auto h = unsigned(s.owner.rear);
							frame->gun =
							    compose({add(solved.wrists[h].position, origin), solved.wrists[h].rotation},
							            inverse(s.controls[h]));
						}
						else
							frame->gun = root_pose(s, hands::attachments::body_frame(solved, origin), *r);
						if (vr::valid_hand(s.magazine_hand))
						{
							const auto h = unsigned(s.magazine_hand);
							const auto& reload = *profile(s.driving.type).reload;
							const auto local =
							    h == 0 ? reload.magazine_in_wrist
							           : hands::pose_mirror::object_in_wrist(
							                 reload.magazine_rest, reload.magazine_in_wrist, s.mirror[h]);
							frame->magazine = compose(
							    {add(solved.wrists[h].position, origin), solved.wrists[h].rotation}, local);
						}
						frame->valid = true;
					}
					else
						frame->retain = !s.owner.can_fire();
				}
			}
			if (part.round && (detached ? s.held_rounds : s.rounds) <= 0)
				return result::omit;
			if (part.magazine && ((!detached && !s.inserted) ||
			                      (detached && (!vr::valid_hand(s.magazine_hand) || s.magazine_grabbed))))
				return result::omit;
			if (!frame->valid)
				return frame->retain && !detached ? result::retain : result::omit;
			auto root = frame->gun;
			if (part.magazine)
			{
				const auto& reload = *profile(s.driving.type).reload;
				if (detached)
					root = frame->magazine;
				else
				{
					root = compose(root, reload.magazine_rest);
					if (s.magazine_grabbed)
						root.position = add(root.position,
						                    rotate(frame->gun.rotation,
						                           rotate(reload.well.rotation, {0, 0, -s.pull * s.units})));
				}
			}
			const auto world = compose(root, part.local);
			std::copy(world.position.begin(), world.position.end(), placed.origin);
			std::copy(world.rotation.begin(), world.rotation.end(), placed.quat);
			previous = placed;
			return result::replace;
		}
		void retire()
		{
			asset_mask = 0;
			const std::lock_guard lock(mutex);
			submissions = {};
			placements = {};
			placement_cursor = 0;
			resources = {};
			attempted = {};
			masked_object = nullptr;
			rendered = {};
			effects = {};
			effect_count = 0;
		}
	}
	void publish_render_hands(const controller_input::frame& input,
	                          const std::array<anchor, 2>& wrists,
	                          vec view_offset) noexcept
	{
		const auto s = latest();
		if (!presentation_allowed() || !s.owner.can_fire() || s.reference != input.reference_generation)
			return;
		const auto h = unsigned(s.owner.rear);
		const auto root =
		    compose({add(wrists[h].position, view_offset), wrists[h].rotation}, inverse(s.controls[h]));
		const std::lock_guard lock(mutex);
		rendered = {
		    s.owner, root, input.reference_generation, input.sequence, ++render_serial, input.sampled_at};
	}
	void queue_effect(const snapshot& s, bool brass) noexcept
	{
		const std::lock_guard lock(mutex);
		if (effect_count == effects.size())
		{
			++fx_rejected;
			return;
		}
		effects[effect_count++] = {
		    s.owner, s.reference, s.sequence, render_serial, controller_input::clock::now(), brass};
	}
	std::string render_status()
	{
		const std::lock_guard lock(mutex);
		return std::format(
		    "frontend_fx_emitted={} frontend_fx_rejected={} pending_fx={} hand_pose_serial={}\n",
		    fx_emitted.load(),
		    fx_rejected.load(),
		    effect_count,
		    render_serial);
	}
	bool assets_ready(kind type) noexcept
	{
		return type != kind::none && (asset_mask.load() & (1u << unsigned(type)));
	}
	bool model_marker(kind type, std::string_view name, anchor& out) noexcept
	{
		if (type == kind::none)
			return false;
		const std::lock_guard lock(mutex);
		const auto& r = resources[type == kind::zodiac ? 0 : 1];
		if (!r)
			return false;
		if (name == "tag_flash")
		{
			out = r->muzzle;
			return true;
		}
		if (name == "tag_brass" && r->has_brass)
		{
			out = r->brass;
			return true;
		}
		return false;
	}
	void mask_native(const void* object, const void* matrices) noexcept
	{
		const auto c = current();
		std::uint32_t epoch{};
		if (!object || !read(object, 0xb0, epoch))
			return;
		if (!c || !presentation_allowed() || !latest().geometry_ready)
		{
			if (object == masked_object)
				weapons::viewmodel_visibility::publish(
				    object, matrices, epoch, {}, weapons::part_visibility::surface);
			return;
		}
		short index{};
		if (!read(reinterpret_cast<void*>(0x14b113080), size_t(c.entity) * 2, index) || index <= 0 ||
		    index >= 4096 || object != reinterpret_cast<void*>(0x14ae2dff0 + size_t(index) * 0x240))
			return;
		unsigned char count{};
		game::XModel** models{};
		if (!read(object, 15, count) || !count || count > 16 || !read(object, 0xd8, models))
			return;
		weapons::part_mask hidden{};
		unsigned offset{};
		for (unsigned i = 0; i < count; ++i)
		{
			game::XModel* model{};
			game::XModel value{};
			if (!read(models, i * sizeof(model), model) || !read(model, 0, value) || !value.name ||
			    offset + value.numBones > 254)
				return;
			const std::string_view name{value.name};
			if (!i && name != vehicle_model(c.type))
				return;
			if (!i && c.type == kind::snowmobile && value.boneNames && matrices)
			{
				int handle = -1;
				for (unsigned b = 0; b < value.numBones; ++b)
					if (const auto* tag = game::SL_ConvertToString(value.boneNames[b]);
					    tag && std::string_view(tag) == "handle")
					{
						if (handle >= 0)
							return;
						handle = int(b);
					}
				std::array<std::uint32_t, 8> calculated{};
				game::DObjAnimMat root{}, bar{};
				const std::byte* view{};
				vec view_offset{};
				if (handle >= 0 && read(object, 0x80, calculated) &&
				    (calculated[unsigned(handle) / 32] & (0x80000000u >> (unsigned(handle) % 32))) &&
				    read(matrices, 0, root) && read(matrices, size_t(handle) * 32, bar) &&
				    read(reinterpret_cast<void*>(0x141e39d30), 0, view) && read(view, 0x58, view_offset))
				{
					auto root_pose = bind(root), handle_pose = bind(bar);
					root_pose.position = add(root_pose.position, view_offset);
					handle_pose.position = add(handle_pose.position, view_offset);
					const auto now = controller_input::clock::now();
					head_pose_bridge::spatial_frame body;
					if (std::isfinite(length(root_pose.position)) &&
					    std::isfinite(length(handle_pose.position)) &&
					    length(sub(root_pose.position, handle_pose.position)) < 200.f &&
					    std::isfinite(dot(rotate(root_pose.rotation, {1, 0, 0}),
					                      rotate(handle_pose.rotation, {1, 0, 0}))) &&
					    head_pose_bridge::get_spatial_frame(body) && body.generation &&
					    now >= body.captured_at && now - body.captured_at <= 150ms &&
					    hands::valid_offset_axis(body.world_yaw_axis))
					{
						const auto reference = inverse({body.world_origin, from_axis(body.world_yaw_axis)});
						publish_handles({c,
						                 compose(reference, root_pose),
						                 compose(reference, handle_pose),
						                 now,
						                 body.generation});
					}
				}
			}
			if (i && (name.starts_with("viewhands_") || name.starts_with("h2_viewmodel_miniuzi") ||
			          name.starts_with("h2_viewmodel_glock")))
				for (unsigned b = 0; b < value.numBones; ++b)
					hidden[(offset + b) / 32] |= 0x80000000u >> ((offset + b) % 32);
			offset += value.numBones;
		}
		masked_object = object;
		weapons::viewmodel_visibility::publish(
		    object, matrices, epoch, hidden, weapons::part_visibility::surface);
	}
	class presentation_component final : public component_interface
	{
		void post_unpack() override
		{
			scheduler::loop(prepare_assets, scheduler::pipeline::main);
			scene_models::on_submit(submit);
			scene_models::on_prepare_placement(place);
			fastfiles::on_pre_unload(retire);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::vehicles::presentation_component)
