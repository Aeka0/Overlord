#include <std_include.hpp>
#include "gameplay/fixed_sniper.hpp"
#include "gameplay/javelin_screen.hpp"
#include "gameplay/notebook_runtime.hpp"
#include "remote_hud_policy.hpp"
#include "native_hud_capture.hpp"
#include "presentation_options.hpp"
#include "native_menu.hpp"
#include "spatial_panel_renderer.hpp"
#include "native_ui_dispatch_bridge.hpp"
#include "native_hud_blend.hpp"
#include <utils/native_memory.hpp>
#include "native_hud_quad.hpp"
#include "narrative_ui.hpp"
#include "damage_screen.hpp"
#include "native_waypoints.hpp"
#include "engine_stereo_draw_indexed.hpp"
#include "gameplay/weapon_interaction.hpp"
#include "gameplay/weapon_hud_lifetime.hpp"
#include "gameplay/weapon_hud_source.hpp"
#include "gameplay/weapon_hud_native.hpp"
#include "gameplay/weapon_carry_runtime.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <d3dcompiler.h>

// Native HUD capture supplies functional stereo layers in every client build.
// Optional readback/export diagnostics remain separately disabled below.
namespace vr::native_hud_capture
{
	namespace
	{
		// Accepted native HUD capture is functional rendering, not a readback test.
		// Keep the old one-shot exporter dormant, including its command and polling.
		constexpr bool readback_diagnostics = false;
		utils::hook::detour dispatch_hook, quad_hook, text_hook, legacy_quad_hook, rotated_quad_hook, stretch_quad_hook, lines_hook, compass_quad_hook;
		std::atomic_bool requested{}, alive{true};
		std::atomic_uint64_t diagnostic_deadline{};
		std::atomic_bool diagnostic_busy{};
		std::atomic_uint64_t dispatches{}, captures{}, copied_draws{}, rejected{};
		std::atomic_uint64_t global_ui_dispatches{},global_ui_captures{},global_ui_failures{};
		std::atomic_uint64_t menu_matches{},menu_draws{},menu_images{},menu_empty_passes{};
		std::atomic_uint64_t selections{}, selection_total_us{}, selection_last_us{};
		std::atomic_uint64_t invalid_targets{}, unsupported_blend{}, unsupported_depth{}, anchors{};
		std::atomic_uint64_t black_subtractive_draws{};
		std::atomic_uint64_t narrative_captures{}, narrative_texts{}, narrative_fades{}, narrative_rejected{};
		std::atomic_uint32_t last_blend_src{}, last_blend_dst{}, last_blend_op{}, last_color{};
		std::atomic_uint64_t last_material{};
		std::atomic_bool report_pending{};
		std::mutex publication_mutex;
		std::shared_ptr<const frame> publication;
		std::array<std::shared_ptr<const frame>,gameplay::weapon_hud::source_count> weapon_publications;
		std::shared_ptr<const frame> narrative_publication, progress_publication, announcement_publication, damage_publication;
		remote_frames remote_publication;
		std::array<std::atomic_uint64_t,remote_hud::frame_count> remote_captures{};
		std::atomic_uint64_t remote_failures{},remote_overflows{};
		screen_scope_frame screen_scope_publication;
		std::shared_ptr<const indicators> indicator_publication;
		std::atomic_uint64_t indicator_captures{}, indicator_rejected{};
		using rectangle=directional_ui::rectangle;
		struct resources
		{
			std::array<std::shared_ptr<frame>, 3> slots;
			std::array<std::array<std::shared_ptr<frame>,3>,menu_surface::surface_count> menu_slots;
			std::array<std::array<std::shared_ptr<frame>,3>,menu_surface::surface_count> menu_canvas_slots;
			spatial_panel::renderer canvas_renderer;
			std::array<std::array<std::shared_ptr<frame>,3>,gameplay::weapon_hud::source_count> weapon_slots;
			std::array<std::shared_ptr<frame>, 3> narrative_slots, narrative_subtractive_slots, progress_slots, announcement_slots, damage_slots;
			std::array<std::shared_ptr<frame>, 3> warning_slots, marker_slots;
			std::array<std::array<std::shared_ptr<frame>,3>,remote_hud::frame_count> remote_slots;
			std::array<std::array<std::shared_ptr<frame>,3>,3> screen_scope_slots;
			struct raster_entry { Microsoft::WRL::ComPtr<ID3D11RasterizerState> original, clipped; };
			std::array<raster_entry,16> rasters;
			std::uint64_t sequence{};
			struct blend_entry
			{
				Microsoft::WRL::ComPtr<ID3D11BlendState> original, coverage, attenuation, subtractive, additive, opaque;
			};
			std::array<blend_entry, 16> blends;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> opaque_alpha;
			Microsoft::WRL::ComPtr<ID3D11BlendState> alpha_only;
			bool alpha_attempted{};
			Microsoft::WRL::ComPtr<ID3D11PixelShader> text_fade;
			Microsoft::WRL::ComPtr<ID3D11Buffer> text_fade_color;
			bool text_fade_attempted{};
			std::uint64_t generation{};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			std::uint64_t staging_at{};
			UINT staging_width{}, staging_height{};
		};
		thread_local resources gpu;
		thread_local std::array<std::array<const void*,1024>,remote_hud::frame_count> remote_selections{};
		struct scope
		{
			unsigned menu_index{menu_surface::surface_count};
			bool fixed_menu_canvas{};
			bool hud_hidden{};
			std::span<const native_menu::range> menu_ranges;
			std::array<const void*, 128> selected{};
			std::span<const void*> extended_selection;
			std::span<const void* const> selections()const noexcept
			{return extended_selection.empty()?std::span<const void* const>(selected):std::span<const void* const>(extended_selection);}
			std::size_t count{};
			rectangle bounds{};
			d3d11::device_snapshot graphics;
			std::shared_ptr<frame> output;
			bool failed{};
			const void* copying_command{};
			unsigned draws{};
			scope* next{};
			scope* fade_copy{};
			bool is_narrative{}, is_progress{}, is_announcement{}, is_damage{},is_narrative_subtractive{};
			bool is_warning{}, is_marker{};
			bool is_remote{};
			unsigned remote_stream{};
			bool is_screen_scope{},scope_border{},is_weapon_screen{};
			unsigned scope_layer{}; // 0: final ink, 1: subtractive shadow, 2: flash below shadow.
			unsigned atlas_x{}, atlas_y{};
			std::array<float,2> native_viewport{};
			const void* fade_command{};
			std::array<narrative_ui::backdrop,spatial_panel::blur_region_capacity> backdrops{};
			unsigned backdrop_count{};
			bool mirror_weapon{};
			const void* weapon_blur{};
			rectangle weapon_border{};
		};
		thread_local scope* active{};
		thread_local scope* copying_scope{};
		thread_local const std::vector<native_waypoints::group>* backdrop_groups{};
		struct menu_pass
		{
			std::shared_ptr<const native_menu::capture_plan> plan;
			std::array<scope,menu_surface::surface_count> scopes;
			bool open{};
		};
		thread_local menu_pass global_ui_pass;
		thread_local std::array<scope,remote_hud::layer_count> global_remote;
		thread_local std::uint64_t global_remote_epoch{},global_remote_reference{};
		thread_local bool global_remote_valid{};
		bool select_remote(const void*,std::span<scope,remote_hud::layer_count>,std::span<const native_menu::range>);
		void prepare_remote(std::span<scope,remote_hud::layer_count> layers,unsigned stream,scope* tail)
		{
			for(unsigned i=0;i<layers.size();++i)
			{
				auto& layer=layers[i];layer.is_remote=true;layer.remote_stream=remote_hud::index(stream,remote_hud::layer(i));
				layer.extended_selection=remote_selections[layer.remote_stream];layer.next=i+1<layers.size()?&layers[i+1]:tail;
			}
		}
		std::uint64_t remote_epoch_for_capture()
		{
			const auto* paused=game::Dvar_FindVar("cl_paused");
			return engine_stereo_bridge::is_active() && game::CL_IsCgameInitialized() && !*game::keyCatchers && paused && !paused->current.integer ?
				gameplay::equipment::special::notebook::camera_epoch():0;
		}
		void publish_remote(scope& captured,unsigned stream,std::uint64_t epoch,std::uint64_t reference,bool valid)
		{
			const std::lock_guard lock(publication_mutex);
			if(!epoch || gameplay::equipment::special::notebook::camera_epoch()!=epoch || !valid || captured.failed)
			{remote_publication[stream].reset();if(epoch && (!valid || captured.failed))++remote_failures;return;}
			// An unrelated empty dispatch must not erase the other UI channel or
			// the same channel's completed draw. Its short lease expires naturally.
			if(!captured.count)return;
			if(!captured.draws || !captured.output){remote_publication[stream].reset();return;}
			auto& out=*captured.output;out.generation=captured.graphics.generation;out.sequence=++gpu.sequence;
			out.timestamp=GetTickCount64();out.context=reinterpret_cast<std::uintptr_t>(captured.graphics.context.Get());
			out.reference_generation=reference;out.remote_camera_epoch=epoch;
			remote_publication[stream]=captured.output;++remote_captures[stream];
		}
		bool prepare_output(scope& s,std::array<std::shared_ptr<frame>,3>& slots);
		void prepare_menu_scopes(menu_pass& pass,const d3d11::device_snapshot& graphics,scope* tail)
		{
			pass.scopes={};
			for(unsigned i=0;i<pass.scopes.size();++i)
			{
				auto& menu=pass.scopes[i];menu.menu_index=i;menu.menu_ranges=pass.plan->ranges;menu.graphics=graphics;
				menu.fixed_menu_canvas=!pass.plan->owner.frontend;
				menu.next=i+1<pass.scopes.size()?&pass.scopes[i+1]:tail;
			}
		}
		void publish_menu_scopes(menu_pass& pass)
		{
			// Excluded native menu effects still carry provenance so another UI
			// consumer cannot mistake them for story fades. They publish no image.
			if(std::none_of(pass.plan->ranges.begin(),pass.plan->ranges.end(),[](const auto& r){return r.surface<menu_surface::surface_count;}))return;
			native_menu::images image;image.owner=pass.plan->owner;
			bool populated{};
			for(unsigned i=0;i<pass.scopes.size();++i)
			{
				auto& menu=pass.scopes[i];if(menu.failed||!menu.draws||!menu.output)continue;
				auto layer=menu.output;
				if(menu.fixed_menu_canvas)
				{
					scope resolved;resolved.graphics=menu.graphics;
					resolved.bounds={0,0,float(ui_canvas::menu_width),float(ui_canvas::menu_height)};
					if(!prepare_output(resolved,gpu.menu_canvas_slots[i]))continue;
					resolved.output->canvas=ui_canvas::fit(layer->width,layer->height);
					if(!gpu.canvas_renderer.draw_canvas(menu.graphics.context.Get(),layer->view.Get(),resolved.output->target.Get(),resolved.output->canvas))continue;
					layer=std::move(resolved.output);
				}
				auto& out=*layer;out.generation=menu.graphics.generation;out.sequence=++gpu.sequence;
				out.timestamp=GetTickCount64();out.context=reinterpret_cast<std::uintptr_t>(menu.graphics.context.Get());
				image.layers[i]=std::move(layer);populated=true;++menu_images;
			}
			if(!populated)++menu_empty_passes;
			native_menu::publish(std::move(image));
		}
		void global_ui_begin(void* commands) noexcept
		{
			++global_ui_dispatches;
			if(!alive||active||global_ui_pass.open)return;
			try
			{
				const auto plan=native_menu::for_stream(reinterpret_cast<std::uintptr_t>(commands));
				const auto remote_epoch=remote_epoch_for_capture();
				if(!plan && !remote_epoch)return;
				const auto graphics=d3d11::get_device_snapshot();if(!graphics)return;
				if(gpu.generation!=graphics.generation){gpu={};gpu.generation=graphics.generation;}
				global_remote={};prepare_remote(global_remote,1,nullptr);global_remote_epoch=remote_epoch;
				for(auto& layer:global_remote)layer.graphics=graphics;
				global_remote_reference=controller_input::latest().reference_generation;
				global_remote_valid=remote_epoch && select_remote(commands,global_remote,
					plan?std::span<const native_menu::range>(plan->ranges):std::span<const native_menu::range>{});
				if(!global_remote_valid)for(auto& layer:global_remote){layer.failed=true;layer.count=0;}
				global_ui_pass.plan=plan;if(plan)prepare_menu_scopes(global_ui_pass,graphics,&global_remote[0]);
				global_ui_pass.open=true;active=plan?&global_ui_pass.scopes[0]:&global_remote[0];
			}
			catch(...){++global_ui_failures;global_ui_pass={};}
		}
		void global_ui_end(void*) noexcept
		{
			if(!global_ui_pass.open)return;
			active=nullptr;
			try
			{
				if(global_ui_pass.plan)publish_menu_scopes(global_ui_pass);
				for(auto& layer:global_remote)publish_remote(layer,layer.remote_stream,global_remote_epoch,global_remote_reference,global_remote_valid);
				++global_ui_captures;
			}
			catch(...){++global_ui_failures;}
			global_ui_pass={};global_remote={};global_remote_epoch=0;
		}

		void arm_report()
		{
			if (report_pending.exchange(true)) return;
			scheduler::schedule([] {
				auto expired = diagnostic_deadline.load();
				if (expired && GetTickCount64() >= expired)
					diagnostic_deadline.compare_exchange_strong(expired, 0);
				std::ostringstream report;
				report << "stage=native_widget_capture; native_flagged_blur_excluded\n"
					<< "dispatches=" << dispatches.load() << " anchors=" << anchors.load()
					<< " captures=" << captures.load() << " draws=" << copied_draws.load()
					<< " rejected=" << rejected.load() << '\n'
					<< "invalid_targets=" << invalid_targets.load() << " unsupported_blend=" << unsupported_blend.load()
					<< " unsupported_depth=" << unsupported_depth.load() << '\n'
					<< "black_subtractive_draws=" << black_subtractive_draws.load()
					<< " last_blend=" << last_blend_src.load() << '/' << last_blend_dst.load() << '/' << last_blend_op.load()
					<< " material=" << std::hex << last_material.load() << " color=" << last_color.load() << std::dec << '\n'
					<< "pending=" << (diagnostic_deadline.load() != 0) << " busy=" << diagnostic_busy.load() << '\n';
				utils::io::write_file_atomic("minidumps/h2-mod-vr-hud-capture.txt", report.str());
				if (!alive.load() || (!diagnostic_deadline.load() && !diagnostic_busy.load()))
				{ report_pending.store(false); return scheduler::cond_end; }
				return scheduler::cond_continue;
			}, scheduler::async, 1s);
		}

		// A bounded one-shot readback, polled only on the renderer owning this
		// immediate context. Encoding/file IO runs off-thread, never under a GPU lock.
		void poll_diagnostic(const d3d11::device_snapshot& graphics)
		{
			if (!gpu.staging) return;
			if (gpu.generation != graphics.generation || GetTickCount64() - gpu.staging_at > 3000)
			{
				gpu.staging.Reset(); diagnostic_busy.store(false);
				console::warn("[VR HUD capture] readback expired; no pixels saved\n");
				return;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto result = graphics.context->Map(gpu.staging.Get(), 0,
				D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING) return;
			if (FAILED(result))
			{
				gpu.staging.Reset(); diagnostic_busy.store(false);
				console::warn("[VR HUD capture] readback failed 0x%08x\n", result); return;
			}
			const auto width = gpu.staging_width, height = gpu.staging_height;
			std::string pixels;
			try
			{
				// Uncompressed top-origin BGRA TGA with eight alpha bits. This is a
				// diagnostic asset, not a packaged replacement for the native widget.
				pixels.resize(18 + static_cast<std::size_t>(width) * height * 4);
				pixels[2] = 2;
				pixels[12] = static_cast<char>(width); pixels[13] = static_cast<char>(width >> 8);
				pixels[14] = static_cast<char>(height); pixels[15] = static_cast<char>(height >> 8);
				pixels[16] = 32; pixels[17] = 0x28;
				for (UINT y = 0; y < height; ++y)
				{
					const auto* row = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
					auto* output = pixels.data() + 18 + static_cast<std::size_t>(y) * width * 4;
					for (UINT x = 0; x < width; ++x)
					{
						// Stored GPU RGB is premultiplied. TGA viewers expect straight alpha.
						const auto alpha = row[x * 4 + 3];
						for (int c = 0; c < 3; ++c)
							output[x * 4 + c] = static_cast<char>(alpha ?
								std::min(255, (row[x * 4 + 2 - c] * 255 + alpha / 2) / alpha) : 0);
						output[x * 4 + 3] = static_cast<char>(alpha);
					}
				}
			}
			catch (...)
			{
				graphics.context->Unmap(gpu.staging.Get(), 0);
				gpu.staging.Reset(); diagnostic_busy.store(false); throw;
			}
			graphics.context->Unmap(gpu.staging.Get(), 0);
			gpu.staging.Reset();
			scheduler::once([pixels = std::move(pixels), width, height] {
				const auto done = gsl::finally([] { diagnostic_busy.store(false); });
				const auto path = "h2-mod/vr/hud-capture-" + std::to_string(GetTickCount64()) + ".tga";
				if (utils::io::write_file(path, pixels))
					console::info("[VR HUD capture] saved %s (%ux%u); blur pass NOT included\n",
						path.c_str(), width, height);
				else console::warn("[VR HUD capture] could not save %s\n", path.c_str());
			}, scheduler::async);
		}

		template<class T> T field(const void* command, std::size_t offset) noexcept
		{
			T value{};
			std::memcpy(&value, static_cast<const std::byte*>(command) + offset, sizeof(value));
			return value;
		}
		std::string_view material_name(const void* command, std::array<char, 96>& storage) noexcept
		{
			const auto material = field<const char**>(command, 8);
			const char* name{};
			if (!utils::native_memory::read_bytes(&name, material, sizeof(name)) ||
				!utils::native_memory::read_bytes(storage.data(), name, storage.size())) return {};
			const auto length = strnlen(storage.data(), storage.size());
			return length < storage.size() ? std::string_view(storage.data(), length) : std::string_view{};
		}
		bool is_black_unlit_quad(const void* command, ID3D11DeviceContext* context) noexcept
		{
			std::array<std::byte, 104> copied{};
			if (!utils::native_memory::read_bytes(copied.data(), command, 4)) return false;
			const auto size=field<std::uint16_t>(copied.data(),0);
			if (!native_hud_quad::is_command(copied.data(),size) ||
				!utils::native_memory::read_bytes(copied.data(),command,size)) return false;
			command = copied.data();
			native_hud_quad::quad quad{};
			if (!native_hud_quad::decode(command,size,quad)) return false;
			std::array<char, 96> name_storage{};
			const auto name = material_name(command, name_storage);
			if (name != "h1_hud_weapwidget_border" && name != "h1_hud_weapwidget_nullnum" &&
				name != "black" && name != "white") return false;
			if (name != "black" && (quad.color & 0xffffff) != 0) return false;
			game::Material material{};
			if (!utils::native_memory::read_bytes(&material, field<const void*>(command, 8), sizeof(material)) ||
				!material.techniqueSet) return false;
			if (name == "black")
			{
				// The Lua go-black image keeps a white tint. Its native solid $black
				// texture, not vertex RGB, establishes the zero subtractive source.
				game::MaterialTextureDef texture{}; game::GfxImage image{}; std::array<char, 16> image_name{};
				if (material.textureCount != 1 || !utils::native_memory::read_bytes(&texture, material.textureTable, sizeof(texture)) ||
					!utils::native_memory::read_bytes(&image, texture.u.image, sizeof(image)) || image.width != 1 || image.height != 1 ||
					!utils::native_memory::read_bytes(image_name.data(), image.name, image_name.size()) ||
					std::string_view(image_name.data(), strnlen(image_name.data(), image_name.size())) != "$black") return false;
			}
			const game::MaterialTechnique* technique_pointer{};
			if (!utils::native_memory::read_bytes(&technique_pointer, reinterpret_cast<const std::byte*>(material.techniqueSet) +
				offsetof(game::MaterialTechniqueSet, techniques) + 8*sizeof(void*), sizeof(technique_pointer))) return false;
			game::MaterialTechnique technique{};
			if (!utils::native_memory::read_bytes(&technique, technique_pointer, sizeof(technique)) || technique.hdr.passCount != 1) return false;
			game::MaterialPixelShader shader{};
			std::array<char, 32> shader_name{};
			if (!utils::native_memory::read_bytes(&shader, technique.passArray[0].pixelShader, sizeof(shader)) ||
				!utils::native_memory::read_bytes(shader_name.data(), shader.name, shader_name.size()) ||
				std::string_view(shader_name.data(), strnlen(shader_name.data(), shader_name.size())) != "unlit_2d.hlsl" ||
				shader.prog.loadDef.microCodeCrc != 0x69f5418b) return false;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> bound;
			context->PSGetShader(&bound, nullptr, nullptr);
			return bound.Get() == shader.prog.ps;
		}
		bool quad_bounds(const void* command, rectangle& rect) noexcept
		{
			rect = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
			for (int vertex = 0; vertex < 4; ++vertex)
			{
				const auto x = field<float>(command, 16 + vertex * 16);
				const auto y = field<float>(command, 20 + vertex * 16);
				if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 32768 || std::abs(y) > 32768)
					return false;
				rect.x = std::min(rect.x, x); rect.y = std::min(rect.y, y);
				rect.right = std::max(rect.right, x); rect.bottom = std::max(rect.bottom, y);
			}
			return rect.right > rect.x && rect.bottom > rect.y;
		}
		bool select_commands(const void* commands, scope& result, unsigned ordinal, unsigned expected,
			std::span<const native_waypoints::group> narrative_groups) noexcept
		{
			// Inspect the bounded immutable owner stream twice: first the native
			// ammo panel's material anchor, then its contained glyph/quad commands.
			bool found{};
			for (int pass = 0; pass < 2; ++pass)
			{
				unsigned borders{};
				const void* pending_blur{};
				auto cursor = static_cast<const std::byte*>(commands);
				bool terminated{};
				for (int n = 0; n < 2048; ++n)
				{
					std::array<std::byte, 4> header{};
					if (!utils::native_memory::read_bytes(header.data(), cursor, header.size())) return false;
					const auto size = field<std::uint16_t>(header.data(), 0);
					const auto op = field<std::uint8_t>(header.data(), 2);
					const auto flags = field<std::uint8_t>(header.data(), 3);
					if (op == 0) { terminated = true; break; }
					if (size < 4 || cursor - static_cast<const std::byte*>(commands) + size > 0x100000) return false;
					bool selected{};
					if ((flags == 0 || flags == 0x40) && op == 17 && size == 104)
					{
						std::array<std::byte, 104> quad{};
						std::array<char, 96> name_storage{};
						if (!utils::native_memory::read_bytes(quad.data(), cursor, quad.size())) return false;
						const auto name = material_name(quad.data(), name_storage);
						rectangle rect{};
						if(pass==0 && flags==0x40 && name=="h1_hud_weapwidget_blur")pending_blur=cursor;
						if (flags==0 && gameplay::weapon_hud::native::border(name) && pass == 0)
						{
							const auto* blur=pending_blur;pending_blur=nullptr;
							if (!(field<std::uint32_t>(quad.data(),96)>>24)) {cursor+=size;continue;}
							if (borders++!=ordinal) {cursor+=size;continue;}
							if (!quad_bounds(quad.data(), rect)) return false;
							result.weapon_blur=blur;result.weapon_border=rect;
							result.bounds = {std::floor(rect.x) - 4, std::floor(rect.y) - 4,
								std::ceil(rect.right) + 4, std::ceil(rect.bottom) + 4};
							found = true;
						}
						if (flags==0 && pass == 1 && name.starts_with("h1_hud_weapwidget_") && quad_bounds(quad.data(), rect))
							selected = rect.x >= result.bounds.x && rect.y >= result.bounds.y &&
								rect.right <= result.bounds.right && rect.bottom <= result.bounds.bottom;
					}
					if (pass == 1 && flags == 0 && op == 20 && size >= 233 && size <= 1024)
					{
						std::array<float, 2> xy{};
						if (!utils::native_memory::read_bytes(xy.data(), cursor + 4, sizeof(xy))) return false;
						const auto x = xy[0], y = xy[1];
						selected = std::isfinite(x) && std::isfinite(y) && x >= result.bounds.x &&
							x <= result.bounds.right && y >= result.bounds.y && y <= result.bounds.bottom;
					}
					if (selected)
					{
						// Proven native script ownership outranks screen-space overlap
						// with either hand's ammo panel. Never carry results with a gun.
						if(native_waypoints::owner(narrative_groups,reinterpret_cast<std::uintptr_t>(cursor),size)!=native_waypoints::text_owner::none)
						{cursor+=size;continue;}
						if (result.count == result.selected.size()) return false;
						result.selected[result.count++] = cursor;
					}
					cursor += size;
				}
				if (!terminated || !found || (pass==0 && borders!=expected)) return false;
			}
			const auto width = result.bounds.right - result.bounds.x;
			const auto height = result.bounds.bottom - result.bounds.y;
			return result.count > 1 && result.bounds.x >= 0 && result.bounds.y >= 0 &&
				width >= 32 && width <= 2048 && height >= 16 && height <= 1024;
		}
		void bind_weapon_blur(scope& s) noexcept
		{
			if(!s.weapon_blur || !s.output)return;
			std::array<std::byte,104> raw{};
			game::Material material{};
			if(!utils::native_memory::read_bytes(raw.data(),s.weapon_blur,raw.size()) ||
				!utils::native_memory::read_bytes(&material,field<const void*>(raw.data(),8),sizeof(material)) ||
				!material.textureTable || !material.textureCount || material.textureCount>16)return;
			// Native WeaponInfoHudDef gives blur and border the same four anchors.
			// Use that border's native extent, not the flagged offscreen coordinates.
			// This route admits a full mask only; never guess an atlas transform.
			if(field<float>(raw.data(),80)!=0 || field<float>(raw.data(),84)!=0 ||
				field<float>(raw.data(),88)!=1 || field<float>(raw.data(),92)!=1)return;
			ID3D11ShaderResourceView* mask{};
			for(unsigned i=0;i<material.textureCount;++i)
			{
				game::MaterialTextureDef texture{};game::GfxImage image{};
				if(!utils::native_memory::read_bytes(&texture,material.textureTable+i,sizeof(texture)))return;
				if(material.textureCount!=1 && texture.semantic!=2)continue;
				if(!utils::native_memory::read_bytes(&image,texture.u.image,sizeof(image)) || image.mapType!=game::MAPTYPE_2D ||
					!image.width || !image.height || !image.texture.shaderView || (mask && mask!=image.texture.shaderView))return;
				mask=image.texture.shaderView;
			}
			if(!mask)return;
			Microsoft::WRL::ComPtr<ID3D11Device> device;mask->GetDevice(&device);
			if(device!=s.graphics.device)return;
			auto& out=*s.output;const auto& border=s.weapon_border;
			out.blur_mask=mask;out.blur_alpha=(field<std::uint32_t>(raw.data(),96)>>24)/255.f;
			out.blur_window={(border.x-s.bounds.x)/out.width,(border.y-s.bounds.y)/out.height,
				(border.right-border.x)/out.width,(border.bottom-border.y)/out.height};
			if(s.mirror_weapon){out.blur_window[0]+=out.blur_window[2];out.blur_window[2]=-out.blur_window[2];}
		}
		bool select_remote(const void* commands,std::span<scope,remote_hud::layer_count> layers,std::span<const native_menu::range> menu_ranges)
		{
			const auto begin=reinterpret_cast<std::uintptr_t>(commands);
			const auto groups=native_waypoints::for_stream(begin,begin+0x100000);
			auto cursor=static_cast<const std::byte*>(commands);
			for(unsigned n=0;n<2048;++n)
			{
				std::array<std::byte,104> raw{};
				if(!utils::native_memory::read_bytes(raw.data(),cursor,4))return false;
				const auto size=field<std::uint16_t>(raw.data(),0);const auto op=field<std::uint8_t>(raw.data(),2);
				if(!op)return true;
				if(size<4 || cursor-static_cast<const std::byte*>(commands)+size>0x100000)return false;
				const auto address=reinterpret_cast<std::uintptr_t>(cursor);bool selected{};
				auto which=remote_hud::layer::instruments;
				if(!field<std::uint8_t>(raw.data(),3) && !native_menu::commands::owner(menu_ranges,address,size))
				{
					if(op==20 && size>=233)
					{
						if(!utils::native_memory::read_bytes(raw.data(),cursor,60))return false;
						selected=native_waypoints::owner(groups,address,size)==native_waypoints::text_owner::none &&
							!narrative_ui::text_command(raw.data(),size) && narrative_ui::text_command(raw.data(),size,true);
					}
					else if(native_hud_quad::is_command(raw.data(),size))
					{
						if(!utils::native_memory::read_bytes(raw.data(),cursor,size))return false;
						std::array<char,96> storage{};const auto name=material_name(raw.data(),storage);
						selected=remote_hud::material(name);if(remote_hud::target_material(name))which=remote_hud::layer::targets;
					}
				}
				if(selected)
				{
					auto& remote=layers[unsigned(which)];
					if(!remote.failed)
					{
						if(remote.count==remote.extended_selection.size()){++remote_overflows;remote.failed=true;remote.count=0;}
						else remote.extended_selection[remote.count++]=cursor;
					}
				}
				cursor+=size;
			}
			return false;
		}
		bool select_narrative(const void* commands, scope& result, scope& subtraction, scope& progress, scope& announcement, scope* damage,
			const std::vector<native_waypoints::group>& groups,std::array<scope,3>* screen_scope,bool weapon_screen,
			std::span<const native_menu::range> menu_ranges)
		{
			bool has_announcement{};
			auto cursor = static_cast<const std::byte*>(commands);
			const bool hidden=!presentation_options::show_hud();
			result.hud_hidden=subtraction.hud_hidden=progress.hud_hidden=announcement.hud_hidden=hidden;
			for (const auto& group:groups) if (!hidden && group.hint_backdrop)
			{
				if (result.backdrop_count==result.backdrops.size()) return false;
				result.backdrops[result.backdrop_count++]=*group.hint_backdrop;
			}
			for (unsigned n = 0; n < 2048; ++n)
			{
				std::array<std::byte, 4> header{};
				if (!utils::native_memory::read_bytes(header.data(), cursor, header.size())) return false;
				const auto size = field<std::uint16_t>(header.data(), 0);
				const auto op = field<std::uint8_t>(header.data(), 2);
				if (!op)
				{
					// Do not allocate a second fullscreen target for fade-only frames.
					if (!has_announcement) announcement.count=0;
					return true; // an empty complete stream clears transient UI
				}
				if (size < 4 || cursor - static_cast<const std::byte*>(commands) + size > 0x100000) return false;
				if(native_menu::commands::owner(menu_ranges,reinterpret_cast<std::uintptr_t>(cursor),size))
				{cursor+=size;continue;}
				bool selected{},progress_selected{},announcement_selected{},fade_selected{},subtraction_selected{};
				if (field<std::uint8_t>(header.data(), 3) == 0)
				{
					if (op == 20 && size >= 233)
					{
						std::array<std::byte, 60> text{};
						if (!utils::native_memory::read_bytes(text.data(), cursor, text.size())) return false;
						const auto address=reinterpret_cast<std::uintptr_t>(cursor);
						const auto owner=native_waypoints::owner(groups,address,size);
						selected = narrative_ui::text_command(text.data(), size, owner!=native_waypoints::text_owner::none);
						progress_selected=selected && owner==native_waypoints::text_owner::progress;
						announcement_selected=selected && native_waypoints::announcement(owner,text.data(),size);
						has_announcement=has_announcement || (!hidden && announcement_selected);
					}
					else if (native_hud_quad::is_command(header.data(),size))
					{
						std::array<std::byte,104> raw{}; std::array<char,96> storage{};
						if (!utils::native_memory::read_bytes(raw.data(),cursor,size)) return false;
						native_hud_quad::quad quad{};
						const auto name = material_name(raw.data(),storage);
						if(screen_scope && (weapon_screen ? gameplay::weapons::javelin_screen::overlay_material(name) : gameplay::fixed_sniper::overlay_material(name)))
						{
							const auto layer=!weapon_screen && name=="h1_hud_overlay_sniperescape_lensshadow"?1u:!weapon_screen && name=="h1_hud_overlay_sniperescape_flash"?2u:0u;
							auto& target=(*screen_scope)[layer];
							if(target.count==target.selected.size())return false;
							target.selected[target.count++]=cursor;
							target.scope_border|=weapon_screen ? name=="hud_javelin_bg" : name=="h1_hud_overlay_sniperescape_scope";
						}
						selected = narrative_ui::fade_quad_command(raw.data(),size,name,quad);
						fade_selected=selected;
						if (!selected && narrative_ui::script_ink_material(name))
						{
							const auto address=reinterpret_cast<std::uintptr_t>(cursor);
							selected=native_waypoints::owns_script_ink(groups,address,size);
							subtraction_selected=narrative_ui::subtractive_script_ink(name,selected);
						}
						if (damage && damage_screen::command(raw.data(),size,name))
						{
							if (damage->count == damage->selected.size()) return false;
							damage->selected[damage->count++] = cursor;
						}
					}
				}
				if (selected && presentation_options::capture_narrative(hidden,fade_selected))
				{
					auto& target=subtraction_selected ? subtraction : progress_selected ? progress : announcement_selected ? announcement : result;
					if (target.count == target.selected.size()) return false;
					target.selected[target.count++] = cursor;
					if (fade_selected)
					{
						if (announcement.count==announcement.selected.size()) return false;
						announcement.selected[announcement.count++]=cursor;
					}
				}
				cursor += size;
			}
			return false;
		}
		bool select_indicators(const void* commands, scope& warnings,
			std::array<scope,directional_ui::waypoint_capacity>& markers, indicators& result)
		{
			using directional_ui::quad_command;
			const auto begin=reinterpret_cast<std::uintptr_t>(commands);
			auto cursor=begin;
			bool terminated{};
			for (unsigned n=0;n<2048;++n)
			{
				std::array<std::byte,104> raw{};
				if (!utils::native_memory::read_bytes(raw.data(),reinterpret_cast<void*>(cursor),4)) return false;
				const auto size=field<std::uint16_t>(raw.data(),0);
				const auto op=field<std::uint8_t>(raw.data(),2);
				if (!op) { terminated=true; break; }
				if (size<4 || cursor-begin+size>0x100000) return false;
				if (!field<std::uint8_t>(raw.data(),3) && quad_command(raw.data(),size))
				{
					if (!utils::native_memory::read_bytes(raw.data(),reinterpret_cast<void*>(cursor),size)) return false;
					std::array<char,96> storage{};
					if (directional_ui::warning_material(material_name(raw.data(),storage)))
					{
						if (warnings.count==warnings.selected.size()) return false;
						warnings.selected[warnings.count++]=reinterpret_cast<void*>(cursor);
					}
				}
				cursor+=size;
			}
			if (!terminated) return false;
			for (const auto& group:native_waypoints::for_stream(begin,cursor))
			{
				if (group.narrative) continue;
				if(result.count==markers.size())break;
				auto& s=markers[result.count];
				s={}; s.is_marker=true; s.bounds={32768,32768,-32768,-32768};
				bool valid=true;
				for (auto p=group.begin;p<group.end;)
				{
					std::array<std::byte,1024> raw{};
					if (!utils::native_memory::read_bytes(raw.data(),reinterpret_cast<void*>(p),4)) { valid=false; break; }
					const auto size=field<std::uint16_t>(raw.data(),0);
					const auto op=field<std::uint8_t>(raw.data(),2);
					if (size<4 || size>raw.size() || p+size>group.end || field<std::uint8_t>(raw.data(),3) ||
						!utils::native_memory::read_bytes(raw.data(),reinterpret_cast<void*>(p),size)) { valid=false; break; }
					directional_ui::rectangle r{};
					if (quad_command(raw.data(),size)) valid=directional_ui::quad_bounds(raw.data(),size,r);
					else if (op==20 && size>=233)
					{
						const float x=field<float>(raw.data(),4), y=field<float>(raw.data(),8);
						const auto height=field<int>(raw.data(),24);
						const float sx=std::abs(field<float>(raw.data(),28)), sy=std::abs(field<float>(raw.data(),32));
						const auto length=strnlen(reinterpret_cast<const char*>(raw.data()+232),size-232);
						valid=std::isfinite(x) && std::isfinite(y) && std::isfinite(sx) && std::isfinite(sy) &&
							height>0 && height<=256 && sx>0 && sx<=8 && sy>0 && sy<=8 && length>0 && length<size-232;
						// Conservative glyph overhang/shadow padding. Capture actual native
						// font draws; byte length only bounds the crop, never rebuilds text.
						r={x-height*sx*.5f,y-height*sy*1.5f,x+height*sx*(length+1),y+height*sy*.5f};
					}
					else valid=false;
					if (!valid || s.count==s.selected.size()) { valid=false; break; }
					s.selected[s.count++]=reinterpret_cast<void*>(p);
					s.bounds.x=std::min(s.bounds.x,r.x); s.bounds.y=std::min(s.bounds.y,r.y);
					s.bounds.right=std::max(s.bounds.right,r.right); s.bounds.bottom=std::max(s.bounds.bottom,r.bottom);
					p+=size;
				}
				// Owned text producers measure with the live font on the frontend.
				// Byte-count bounds overestimate full/localized prompts enough to
				// reject the entire tile. Still validate every command and fingerprint.
				if (!valid || !s.count || !directional_ui::marker_crop(group.measured_bounds.value_or(s.bounds),s.bounds))
				{ s={}; ++indicator_rejected; continue; }
				auto& marker=result.markers[result.count]; marker=group.marker;
				s.native_viewport=marker.viewport;
				marker.crop={s.bounds.x,s.bounds.y,s.bounds.right,s.bounds.bottom};
				s.atlas_x=(result.count%4)*directional_ui::tile_width;
				s.atlas_y=(result.count/4)*directional_ui::tile_height;
				++result.count;
			}
			return true;
		}
		bool prepare_output(scope& s, std::array<std::shared_ptr<frame>, 3>& slots)
		{
			for (auto& slot : slots)
			{
				if (!slot) slot = std::make_shared<frame>();
				if (slot.use_count() == 1) { s.output = slot; break; }
			}
			if (!s.output) return false;
			auto& out = *s.output;
			const auto width = static_cast<UINT>(s.bounds.right - s.bounds.x);
			const auto height = static_cast<UINT>(s.bounds.bottom - s.bounds.y);
			if (!out.texture || out.width != width || out.height != height)
			{
				out = {};
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (FAILED(s.graphics.device->CreateTexture2D(&desc, nullptr, &out.texture)) ||
					FAILED(s.graphics.device->CreateRenderTargetView(out.texture.Get(), nullptr, &out.target)) ||
					FAILED(s.graphics.device->CreateShaderResourceView(out.texture.Get(), nullptr, &out.view)))
				{ out = {}; return false; }
				out.width = width; out.height = height;
			}
			out.fade = {};
			out.blur_mask.Reset();out.blur_alpha=0;out.blur_window={0,0,1,1};
			out.backdrop_count=0;
			out.hud_hidden=s.hud_hidden;
			out.canvas={};
			out.subtractive.reset();
			const float transparent[4]{};
			s.graphics.context->ClearRenderTargetView(out.target.Get(), transparent);
			return true;
		}
		void copy_draw_to(scope& s,ID3D11DeviceContext* context, UINT count, UINT start, INT base,
			engine_stereo_draw_indexed::draw_original original) noexcept
		{
			if (s.failed || context != s.graphics.context.Get()) return;
			std::array<ID3D11RenderTargetView*, 8> targets{};
			std::array<ID3D11UnorderedAccessView*, 8> uavs{};
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
			Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state;
			UINT stencil_reference{};
			FLOAT factors[4]{}; UINT mask{};
			std::array<D3D11_VIEWPORT, 16> viewports{}; UINT viewport_count = 16;
			std::array<D3D11_RECT, 16> scissors{}; UINT scissor_count = 16;
			context->OMGetRenderTargets(8, targets.data(), &depth);
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 8, uavs.data());
			const auto release = gsl::finally([&] {
				for (auto* target : targets) if (target) target->Release();
				for (auto* uav : uavs) if (uav) uav->Release();
			});
			context->OMGetBlendState(&blend, factors, &mask);
			context->OMGetDepthStencilState(&depth_state, &stencil_reference);
			context->RSGetViewports(&viewport_count, viewports.data());
			context->RSGetScissorRects(&scissor_count, scissors.data());
			native_hud_quad::quad quad{};
			const bool image_command=native_hud_quad::decode(s.copying_command,
				field<std::uint16_t>(s.copying_command,0),quad);
			std::array<char,96> image_name{};
			const auto image_material=s.is_narrative && image_command?material_name(s.copying_command,image_name):std::string_view{};
			const bool script_ink=s.is_narrative && image_command &&
				narrative_ui::script_ink_material(image_material);
			const bool fade_image=s.is_narrative && image_command && !script_ink;
			if (fade_image)
			{
				if (viewport_count != 1) return;
				const auto& v = viewports[0];
				if (!narrative_ui::covers_viewport(quad.vertices, 0, 0, v.Width, v.Height)) return;
				// Scissor rectangles are irrelevant when the rasterizer disables them.
				Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
				context->RSGetState(&raster);
				D3D11_RASTERIZER_DESC raster_desc{};
				if (raster) raster->GetDesc(&raster_desc);
				if (raster_desc.ScissorEnable)
				{
					if (scissor_count != 1) return;
					if (scissors[0].left > 0 || scissors[0].top > 0 || scissors[0].right < v.Width || scissors[0].bottom < v.Height) return;
				}
			}
			if (viewport_count != 1 || !targets[0] || !blend)
			{ ++invalid_targets; s.failed = true; return; }
			if (s.is_marker && (viewports[0].Width!=s.native_viewport[0] || viewports[0].Height!=s.native_viewport[1]))
			{ ++invalid_targets; s.failed=true; return; }
			for (size_t i = 1; i < targets.size(); ++i) if (targets[i])
			{ ++invalid_targets; s.failed = true; return; }
			for (auto* uav : uavs) if (uav)
			{ ++invalid_targets; s.failed = true; return; }
			if (depth)
			{
				D3D11_DEPTH_STENCIL_DESC state{};
				if (depth_state) depth_state->GetDesc(&state);
				// Null state is D3D's depth-enabled default. A merely bound, disabled
				// DSV is harmless; real depth/stencil effects need their own route.
				if (!depth_state || state.DepthEnable || state.StencilEnable)
				{ ++unsupported_depth; s.failed = true; return; }
			}
			D3D11_BLEND_DESC desc{}; blend->GetDesc(&desc);
			const auto& source = desc.RenderTarget[0];
			D3D11_BLEND_DESC coverage_desc{};
			last_blend_src.store(source.SrcBlend); last_blend_dst.store(source.DestBlend);
			last_blend_op.store(source.BlendOp);
			last_material.store(image_command ? quad.material : 0);
			last_color.store(image_command ? quad.color : 0xffffffff);
			const bool black_unlit = source.BlendOp == D3D11_BLEND_OP_REV_SUBTRACT &&
				is_black_unlit_quad(s.copying_command, context);
			const bool subtractive=s.is_narrative_subtractive || (s.is_screen_scope && !s.is_weapon_screen && s.scope_layer==1);
			const bool menu=s.menu_index<menu_surface::surface_count;
			const bool additive=(menu || s.is_remote || (s.is_screen_scope && (s.is_weapon_screen || s.scope_layer==2))) && source.BlendEnable && source.DestBlend==D3D11_BLEND_ONE;
			const bool opaque=(menu || s.is_remote || s.is_weapon_screen) && !source.BlendEnable && !desc.AlphaToCoverageEnable && (source.RenderTargetWriteMask&15)==15;
			if(opaque)
			{
				coverage_desc=desc;coverage_desc.RenderTarget[0].RenderTargetWriteMask=7;
				// Native replace-mode quads are opaque even if their texture's alpha
				// is zero. Retain the original RGB shader, then stamp coverage only.
				if(!gpu.alpha_attempted)
				{
					gpu.alpha_attempted=true;Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
					constexpr char shader[]="float4 main():SV_Target{return float4(0,0,0,1);}";
					D3D11_BLEND_DESC alpha{};alpha.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALPHA;
					if(SUCCEEDED(D3DCompile(shader,sizeof(shader)-1,"native-opaque-coverage",nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)))
						s.graphics.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gpu.opaque_alpha);
					s.graphics.device->CreateBlendState(&alpha,&gpu.alpha_only);
				}
				if(!gpu.opaque_alpha || !gpu.alpha_only){s.failed=true;return;}
			}
			if (!opaque && !(subtractive ? subtractive_coverage_blend(desc,coverage_desc) : additive ? additive_coverage_blend(desc,coverage_desc) : coverage_blend(desc, black_unlit, coverage_desc)))
			{ ++unsupported_blend; s.failed = true; return; }
			const bool attenuating=s.is_announcement && image_command;
			if (attenuating)
			{
				attenuate_text_blend(coverage_desc);
				if(!gpu.text_fade_attempted)
				{
					gpu.text_fade_attempted=true;Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
					constexpr char shader[]="cbuffer Fade:register(b0){float4 color;} float4 main():SV_Target{return color;}";
					if(SUCCEEDED(D3DCompile(shader,sizeof(shader)-1,"native-text-fade",nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)))
						s.graphics.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&gpu.text_fade);
					D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.Usage=D3D11_USAGE_DYNAMIC;
					buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
					s.graphics.device->CreateBuffer(&buffer,nullptr,&gpu.text_fade_color);
				}
				if(!gpu.text_fade || !gpu.text_fade_color){s.failed=true;return;}
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if(FAILED(context->Map(gpu.text_fade_color.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){s.failed=true;return;}
				const auto color=narrative_ui::fade_color(image_material,quad.color);
				std::memcpy(mapped.pData,color.data(),sizeof(color));context->Unmap(gpu.text_fade_color.Get(),0);
			}
			ID3D11BlendState* coverage{};
			for (auto& entry : gpu.blends)
			{
				if (entry.original && entry.original.Get()!=blend.Get()) continue;
				auto& cached=opaque ? entry.opaque : subtractive ? entry.subtractive : additive ? entry.additive : attenuating ? entry.attenuation : entry.coverage;
				if (!cached && FAILED(s.graphics.device->CreateBlendState(&coverage_desc,&cached))) { s.failed = true; return; }
				entry.original = blend; coverage = cached.Get(); break;
			}
			if (!coverage) { s.failed = true; return; }
			if (menu || s.is_narrative || s.is_warning || s.is_damage || s.is_screen_scope || s.is_remote)
			{
				const auto& v = viewports[0];
				if (!std::isfinite(v.Width) || !std::isfinite(v.Height) || v.Width < 16 || v.Height < 16 ||
					v.Width > 8192 || v.Height > 8192 || v.Width*v.Height > (menu ? 8388608 : 16777216) ||
					v.TopLeftX != 0 || v.TopLeftY != 0 || v.Width != std::floor(v.Width) || v.Height != std::floor(v.Height))
				{ s.failed = true; return; }
				if (s.output && (s.output->width != v.Width || s.output->height != v.Height))
				{ s.failed = true; return; }
				if (!s.output)
				{
					s.bounds = {0,0,v.Width,v.Height};
					try { if (!prepare_output(s, menu ? gpu.menu_slots[s.menu_index] : s.is_remote ? gpu.remote_slots[s.remote_stream] : s.is_screen_scope ? gpu.screen_scope_slots[s.scope_layer] : s.is_damage ? gpu.damage_slots : s.is_warning ? gpu.warning_slots :
						s.is_narrative_subtractive ? gpu.narrative_subtractive_slots : s.is_progress ? gpu.progress_slots : s.is_announcement ? gpu.announcement_slots : gpu.narrative_slots)) { s.failed = true; return; } }
					catch (...) { s.failed = true; return; }
				}
				if (fade_image && !s.is_announcement && s.fade_command != s.copying_command)
				{
					narrative_ui::append_fade(s.output->fade,narrative_ui::fade_color(image_material,quad.color));
					s.fade_command = s.copying_command;
					++narrative_fades;
				}
				if (s.is_narrative && !image_command) ++narrative_texts;
			}
			auto viewport = viewports[0];
			const float offset_x=s.bounds.x-s.atlas_x, offset_y=s.bounds.y-s.atlas_y;
			viewport.TopLeftX -= offset_x; viewport.TopLeftY -= offset_y;
			auto translated_scissors = scissors;
			for (UINT i = 0; i < scissor_count; ++i)
			{
				translated_scissors[i].left -= static_cast<LONG>(offset_x);
				translated_scissors[i].right -= static_cast<LONG>(offset_x);
				translated_scissors[i].top -= static_cast<LONG>(offset_y);
				translated_scissors[i].bottom -= static_cast<LONG>(offset_y);
			}
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> original_raster;
			UINT translated_count=scissor_count;
			if (s.is_marker)
			{
				context->RSGetState(&original_raster);
				if (!original_raster) { s.failed=true; return; }
				D3D11_RASTERIZER_DESC raster{}; original_raster->GetDesc(&raster);
				D3D11_RECT tile{static_cast<LONG>(s.atlas_x),static_cast<LONG>(s.atlas_y),
					static_cast<LONG>(s.atlas_x+s.bounds.right-s.bounds.x),static_cast<LONG>(s.atlas_y+s.bounds.bottom-s.bounds.y)};
				if (raster.ScissorEnable)
				{
					if (scissor_count!=1) { s.failed=true; return; }
					tile.left=std::max(tile.left,translated_scissors[0].left); tile.top=std::max(tile.top,translated_scissors[0].top);
					tile.right=std::max(tile.left,std::min(tile.right,translated_scissors[0].right));
					tile.bottom=std::max(tile.top,std::min(tile.bottom,translated_scissors[0].bottom));
				}
				ID3D11RasterizerState* clipped{};
				for (auto& entry:gpu.rasters)
				{
					if (entry.original.Get()==original_raster.Get()) { clipped=entry.clipped.Get(); break; }
					if (entry.original) continue;
					raster.ScissorEnable=TRUE;
					if (FAILED(s.graphics.device->CreateRasterizerState(&raster,&entry.clipped))) { s.failed=true; return; }
					entry.original=original_raster; clipped=entry.clipped.Get(); break;
				}
				if (!clipped) { s.failed=true; return; }
				translated_scissors[0]=tile; translated_count=1; context->RSSetState(clipped);
			}
			auto* target = s.output->target.Get();
			context->OMSetRenderTargets(1, &target, nullptr);
			context->OMSetBlendState(coverage, factors, mask);
			context->RSSetViewports(1, &viewport);
			context->RSSetScissorRects(translated_count, translated_scissors.data());
			if(attenuating)
			{
				Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
				Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
				std::array<ID3D11ClassInstance*,256> classes{};UINT class_count=UINT(classes.size());
				context->PSGetShader(&shader,classes.data(),&class_count);context->PSGetConstantBuffers(0,1,&buffer);
				auto* color=gpu.text_fade_color.Get();context->PSSetConstantBuffers(0,1,&color);
				context->PSSetShader(gpu.text_fade.Get(),nullptr,0);
				original(context,count,start,base);
				context->PSSetShader(shader.Get(),classes.data(),class_count);
				auto* previous=buffer.Get();context->PSSetConstantBuffers(0,1,&previous);
				for(UINT i=0;i<class_count;++i)if(classes[i])classes[i]->Release();
			}
			else original(context, count, start, base);
			if(opaque)
			{
				Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
				std::array<ID3D11ClassInstance*,256> classes{};UINT class_count=UINT(classes.size());
				context->PSGetShader(&shader,classes.data(),&class_count);
				context->PSSetShader(gpu.opaque_alpha.Get(),nullptr,0);context->OMSetBlendState(gpu.alpha_only.Get(),factors,mask);
				original(context,count,start,base);
				context->PSSetShader(shader.Get(),classes.data(),class_count);
				for(UINT i=0;i<class_count;++i)if(classes[i])classes[i]->Release();
			}
			context->OMSetRenderTargets(8, targets.data(), depth.Get());
			context->OMSetBlendState(blend.Get(), factors, mask);
			context->RSSetViewports(viewport_count, viewports.data());
			context->RSSetScissorRects(scissor_count, scissors.data());
			if (s.is_marker) context->RSSetState(original_raster.Get());
			++s.draws; ++copied_draws;
			if(s.menu_index<menu_surface::surface_count)++menu_draws;
			if (source.BlendOp == D3D11_BLEND_OP_REV_SUBTRACT) ++black_subtractive_draws;
		}
		void copy_draw(ID3D11DeviceContext* context,UINT count,UINT start,INT base,
			engine_stereo_draw_indexed::draw_original original) noexcept
		{
			if (!copying_scope) return;
			auto& source=*copying_scope;
			copy_draw_to(source,context,count,start,base,original);
			// Mirror only selected native fades into the announcement ink. The
			// native command handler runs once; both copies retain its draw order.
			auto* target=source.fade_copy;
			if (target && std::find(target->selected.begin(),target->selected.begin()+target->count,source.copying_command)!=
				target->selected.begin()+target->count)
			{
				target->copying_command=source.copying_command;
				copy_draw_to(*target,context,count,start,base,original);
			}
		}
		void handle(void** cursor, utils::hook::detour& hook)
		{
			scope* selected{};
			const auto menu_owns=[&](const scope& s)
			{
				const auto p=reinterpret_cast<std::uintptr_t>(*cursor);
				auto it=std::upper_bound(s.menu_ranges.begin(),s.menu_ranges.end(),p,[](auto at,const auto& r){return at<r.begin;});
				if(it==s.menu_ranges.begin())return false;--it;
				return it->surface==s.menu_index&&p<it->end&&field<std::uint16_t>(*cursor,0)<=it->end-p;
			};
			if (!copying_scope) for (auto* s = active; s; s = s->next)
				if (!s->failed && (std::find(s->selections().begin(), s->selections().begin()+s->count, *cursor) != s->selections().begin()+s->count ||
					menu_owns(*s)))
				{ selected = s; break; }
			if (!selected) { hook.invoke<void>(cursor); return; }
			if(selected->menu_index<menu_surface::surface_count)++menu_matches;
			// Flush pending unrelated tessellation before selecting the primitive.
			if (*reinterpret_cast<const unsigned*>(0x151A7F378)) game::RB_EndTessSurface();
			// Shader binding is established by the native handler/flush, not here.
			const auto* selected_command = *cursor;
			copying_scope = selected;
			const auto exit = gsl::finally([] { copying_scope = nullptr; });
			selected->copying_command = selected_command;
			// Private scratch keeps the submitted stream immutable. Native handlers
			// still shape and draw all glyphs, including localized text and shadows.
			if(selected->mirror_weapon)
			{
				std::array<std::byte,1024> scratch{};
				const auto size=field<std::uint16_t>(selected_command,0);
				bool mirrored=size<=scratch.size() && utils::native_memory::read_bytes(scratch.data(),selected_command,size);
				const auto bytes=std::span(scratch).first(std::min<std::size_t>(size,scratch.size()));
				const float axis_sum=selected->weapon_border.x+selected->weapon_border.right;
				if(mirrored && field<std::uint8_t>(scratch.data(),2)==17)
				{
					std::array<char,96> storage{};
					mirrored=gameplay::weapon_hud::native::mirror_quad(bytes,axis_sum,
						gameplay::weapon_hud::native::upright_image(material_name(scratch.data(),storage)));
				}
				else if(mirrored && size>=233 && field<std::uint8_t>(scratch.data(),2)==20)
				{
					const auto* text=reinterpret_cast<const char*>(scratch.data()+232);
					const auto length=strnlen(text,size-232);
					auto* font=field<game::Font_s*>(scratch.data(),16);
					mirrored=length>0 && length<size-232 && font && !(field<std::uint32_t>(scratch.data(),56)&0x4000);
					if(mirrored)mirrored=gameplay::weapon_hud::native::mirror_text(bytes,axis_sum,
						float(game::R_TextWidth(text,int(length),font)));
				}
				else mirrored=false;
				if(!mirrored){selected->failed=true;hook.invoke<void>(cursor);return;}
				selected->copying_command=scratch.data();
				void* transformed=scratch.data();hook.invoke<void>(&transformed);
				*cursor=static_cast<std::byte*>(*cursor)+size;
				// Complete all reads/draws before the private inline text expires.
				if (*reinterpret_cast<const unsigned*>(0x151A7F378)) game::RB_EndTessSurface();
				return;
			}
			else hook.invoke<void>(cursor);
			if (*reinterpret_cast<const unsigned*>(0x151A7F378)) game::RB_EndTessSurface();
		}
		void quad_stub(void** cursor)
		{
			if (backdrop_groups)
			{
				std::array<std::byte,104> raw{};std::array<char,96> name{};
				const auto address=reinterpret_cast<std::uintptr_t>(*cursor);
				if (native_waypoints::owns_hint_backdrop(*backdrop_groups,address,raw.size()) &&
					utils::native_memory::read_bytes(raw.data(),*cursor,raw.size()) && field<std::uint16_t>(raw.data(),0)==raw.size() &&
					field<std::uint8_t>(raw.data(),2)==17 && field<std::uint8_t>(raw.data(),3)==0x40 &&
					narrative_ui::hint_blur_material(material_name(raw.data(),name)))
				{
					// Same cursor advance as the native handler's no-draw branch.
					*cursor=static_cast<std::byte*>(*cursor)+raw.size();return;
				}
			}
			handle(cursor,quad_hook);
		}
		void text_stub(void** cursor) { handle(cursor, text_hook); }
		void legacy_quad_stub(void** cursor) { handle(cursor, legacy_quad_hook); }
		void rotated_quad_stub(void** cursor) { handle(cursor, rotated_quad_hook); }
		void stretch_quad_stub(void** cursor) { handle(cursor, stretch_quad_hook); }
		void lines_stub(void** cursor) { handle(cursor, lines_hook); }
		void compass_quad_stub(void** cursor) { handle(cursor, compass_quad_hook); }
		void dispatch_stub(void* commands, const int* filter, bool flagged)
		{
			++dispatches;
			const auto menu_plan=native_menu::for_stream(reinterpret_cast<std::uintptr_t>(commands));
			const auto native = [&] { dispatch_hook.invoke<void>(commands, filter, flagged); };
			bool diagnostic{};
			if constexpr (readback_diagnostics)
			{
				if (!active && gpu.staging)
				{
					const auto graphics = d3d11::get_device_snapshot();
					if (graphics) poll_diagnostic(graphics);
				}
				const auto deadline = diagnostic_deadline.load();
				diagnostic = deadline && GetTickCount64() < deadline;
				if (deadline && !diagnostic && diagnostic_deadline.exchange(0))
					console::warn("[VR HUD capture] request expired without a complete capture\n");
			}
			const bool narrative_enabled = engine_stereo_bridge::is_active();
			if (active || (!menu_plan && !requested.load() && !diagnostic && !narrative_enabled) || !alive.load())
			{ native(); return; }
			const auto begin=reinterpret_cast<std::uintptr_t>(commands);
			const auto groups=narrative_enabled ? native_waypoints::for_stream(begin,begin+0x100000) : std::vector<native_waypoints::group>{};
			const auto* previous_groups=backdrop_groups;
			backdrop_groups=narrative_enabled ? &groups : nullptr;
			const auto restore_groups=gsl::finally([&]{backdrop_groups=previous_groups;});
			if (flagged || filter) {native();return;}
			const auto* paused = game::Dvar_FindVar("cl_paused");
			const bool gameplay = game::CL_IsCgameInitialized() && !*game::keyCatchers && paused && !paused->current.integer;
			const auto remote_epoch=gameplay ? gameplay::equipment::special::notebook::camera_epoch() : 0;
			std::array<scope,remote_hud::layer_count> remote_layers{};
			auto& remote=remote_layers[0];prepare_remote(remote_layers,0,nullptr);
			std::array<scope,gameplay::weapon_hud::source_count> weapon_scopes{};
			auto& s=weapon_scopes[0];
			scope narrative{};
			scope subtraction{};subtraction.is_narrative=subtraction.is_narrative_subtractive=true;
			scope progress{};progress.is_narrative=progress.is_progress=true;
			scope announcement{};announcement.is_narrative=announcement.is_announcement=true;
			narrative.fade_copy=&announcement;
			scope damage{}; damage.is_damage = true;
			std::array<scope,3> screen_scopes{};
			for(unsigned i=0;i<screen_scopes.size();++i){screen_scopes[i].is_screen_scope=true;screen_scopes[i].scope_layer=i;}
			auto& screen_scope=screen_scopes[0];
			const auto scope_owner=gameplay::fixed_sniper::current();
			const auto display_epoch=gameplay::weapons::javelin_screen::camera_epoch();
			const auto display_owner=scope_owner.epoch ? gameplay::weapons::hold{} : gameplay::weapons::javelin_screen::capture_owner();
			const bool weapon_screen=display_owner.can_fire();
			for(auto& layer:screen_scopes)layer.is_weapon_screen=weapon_screen;
			scope warnings{}; warnings.is_warning=true;
			std::array<scope,directional_ui::waypoint_capacity> markers{};
			auto indicator_result=std::make_shared<indicators>();
			narrative.is_narrative = true;
			const auto selection_start = std::chrono::steady_clock::now();
			const bool independent=gameplay::weapons::carry::active();
			std::array<gameplay::weapon_hud::source_snapshot,gameplay::weapon_hud::source_count> capture_sources{};
			std::array<bool,gameplay::weapon_hud::source_count> selected_sources{};
			const auto capture_reference = controller_input::latest().reference_generation;
			unsigned source_count{};
			if (independent)
			{
				const auto sources=gameplay::weapon_hud::source_owners();
				for(const auto& source:sources)
					if (source.reference==capture_reference && gameplay::weapon_hud::same_presentation_owner(source.owner,
						gameplay::weapons::carry::held(source.owner.id()))) capture_sources[source_count++]=source;
			}
			else {capture_sources[0].owner=gameplay::weapons::current_hold();capture_sources[0].definition=capture_sources[0].owner.weapon;source_count=1;}
			bool selected{};
			for (unsigned i=0;i<source_count;++i)
			{
				selected_sources[i]=gameplay && gameplay::weapon_hud::has_presentation_owner(capture_sources[i].owner) &&
					(requested.load() || diagnostic) && select_commands(commands,weapon_scopes[i],i,source_count,groups);
				if (!selected_sources[i]) weapon_scopes[i].count=0;
				selected=selected || selected_sources[i];
			}
			const bool remote_valid=remote_epoch && select_remote(commands,remote_layers,
				menu_plan?std::span<const native_menu::range>(menu_plan->ranges):std::span<const native_menu::range>{});
			if(!remote_valid)for(auto& layer:remote_layers){layer.failed=true;layer.count=0;}
			const bool remote_selected=std::any_of(remote_layers.begin(),remote_layers.end(),[](const scope& value){return value.count!=0;});
			const bool narrative_valid = narrative_enabled && select_narrative(commands, narrative, subtraction, progress, announcement, gameplay ? &damage : nullptr,groups,
				scope_owner.epoch || weapon_screen ? &screen_scopes : nullptr,weapon_screen,
				menu_plan?std::span<const native_menu::range>(menu_plan->ranges):std::span<const native_menu::range>{});
			if(!narrative_valid || !screen_scope.scope_border)for(auto& layer:screen_scopes)layer.count=0;
			const auto remote_done=gsl::finally([&]{for(auto& layer:remote_layers)publish_remote(layer,layer.remote_stream,remote_epoch,capture_reference,remote_valid);});
			const auto publish_scope=gsl::finally([&] {
				const std::lock_guard lock(publication_mutex);
				if(narrative_valid && (scope_owner.epoch || (weapon_screen && display_epoch &&
					gameplay::weapons::javelin_screen::camera_epoch()==display_epoch && gameplay::weapons::javelin_screen::requested(display_owner))) && screen_scope.scope_border && screen_scope.draws &&
					std::all_of(screen_scopes.begin(),screen_scopes.end(),[](const scope& layer){return !layer.failed && (!layer.count || (layer.draws && layer.output));}))
				{
					for(auto& layer:screen_scopes)if(layer.output)
					{
						auto& out=*layer.output;out.generation=layer.graphics.generation;out.sequence=++gpu.sequence;
						out.timestamp=GetTickCount64();out.context=reinterpret_cast<std::uintptr_t>(layer.graphics.context.Get());
						out.screen_scope_epoch=scope_owner.epoch;out.reference_generation=capture_reference;
						out.weapon=display_owner.weapon;out.instance_generation=display_owner.instance_generation;out.rear_revision=display_owner.rear_revision;
						out.weapon_display_epoch=weapon_screen?display_epoch:0;
					}
					screen_scope_publication={screen_scope.output,screen_scopes[1].output,screen_scopes[2].output};
				}
				else screen_scope_publication={};
			});
			if (!narrative_valid) { narrative.count = progress.count = announcement.count = damage.count = 0; }
			const auto publish_damage = gsl::finally([&] {
				const std::lock_guard lock(publication_mutex);
				if (narrative_valid && !damage.failed && damage.draws && damage.output)
				{
					auto& out = *damage.output;
					out.generation = damage.graphics.generation; out.sequence = ++gpu.sequence;
					out.timestamp = GetTickCount64();
					out.reference_generation = capture_reference;
					out.context = reinterpret_cast<std::uintptr_t>(damage.graphics.context.Get());
					damage_publication = damage.output;
				}
				else damage_publication.reset();
			});
			const bool indicators_valid=narrative_enabled && gameplay && select_indicators(commands,warnings,markers,*indicator_result);
			if (!indicators_valid) { warnings.count=0; indicator_result->count=0; }
			else
			{
				// World labels can project over the native ammo rectangle. Their exact
				// producer ranges own those commands; coordinate-based HUD selection
				// must not also copy the text onto a weapon or head-relative panel.
				const auto is_marker=[&](const void* command) {
					for (unsigned i=0;i<indicator_result->count;++i)
						if (std::find(markers[i].selected.begin(),markers[i].selected.begin()+markers[i].count,command)!=
							markers[i].selected.begin()+markers[i].count) return true;
					return false;
				};
				const auto remove_markers=[&](scope& panel) {
					panel.count=std::remove_if(panel.selected.begin(),panel.selected.begin()+panel.count,is_marker)-panel.selected.begin();
				};
				for (auto& panel:weapon_scopes) remove_markers(panel);
				remove_markers(narrative);
				remove_markers(progress);
				remove_markers(announcement);
			}
			const auto publish_indicators=gsl::finally([&] {
				const auto stamp=[&](scope& item) {
					if (!item.output || item.failed || !item.draws) return false;
					auto& out=*item.output; out.generation=item.graphics.generation; out.sequence=++gpu.sequence;
					out.timestamp=GetTickCount64(); out.context=reinterpret_cast<std::uintptr_t>(item.graphics.context.Get());
					return true;
				};
				if (indicators_valid)
				{
					if (stamp(warnings)) indicator_result->warnings=warnings.output;
					for (unsigned i=0;i<indicator_result->count;++i)
					{
						if (stamp(markers[i])) indicator_result->atlas=markers[i].output;
						else { indicator_result->markers[i].crop={}; ++indicator_rejected; }
					}
				}
				const std::lock_guard lock(publication_mutex);
				if (indicator_result->warnings || indicator_result->atlas) { indicator_publication=indicator_result; ++indicator_captures; }
				else indicator_publication.reset();
			});
			// Clear failures/empty streams too; transient text cannot keep old ink.
			const auto publish_narrative = gsl::finally([&] {
				if (!narrative_enabled) return;
				const std::lock_guard lock(publication_mutex);
				{
					if (narrative_valid && !narrative.failed && !announcement.failed && announcement.draws && announcement.output)
					{
						auto& out=*announcement.output;out.generation=announcement.graphics.generation;out.sequence=++gpu.sequence;
						out.timestamp=GetTickCount64();out.context=reinterpret_cast<std::uintptr_t>(announcement.graphics.context.Get());
						announcement_publication=announcement.output;
					}
					else announcement_publication.reset();
				}
				{
					if(narrative_valid && !progress.failed && progress.draws && progress.output)
					{
						auto& out=*progress.output;out.generation=progress.graphics.generation;out.sequence=++gpu.sequence;
						out.timestamp=GetTickCount64();out.context=reinterpret_cast<std::uintptr_t>(progress.graphics.context.Get());progress_publication=progress.output;
					}
					else progress_publication.reset();
				}
				if (narrative_valid && !narrative.failed && narrative.draws && narrative.output)
				{
					auto& out = *narrative.output;
					out.backdrops=narrative.backdrops;out.backdrop_count=narrative.backdrop_count;
					out.generation = narrative.graphics.generation; out.sequence = ++gpu.sequence;
					out.timestamp = GetTickCount64();
					out.context = reinterpret_cast<std::uintptr_t>(narrative.graphics.context.Get());
					// A rejected backing cannot erase valid text. Publish it only as
					// an immutable companion from this exact completed stream/device.
					if(!subtraction.failed && subtraction.draws && subtraction.output &&
						subtraction.output->width==out.width && subtraction.output->height==out.height)
					{
						auto& backing=*subtraction.output;backing.generation=out.generation;backing.sequence=out.sequence;
						backing.timestamp=out.timestamp;backing.context=out.context;out.subtractive=subtraction.output;
					}
					narrative_publication = narrative.output;
					++narrative_captures;
				}
				else
				{
					if (!narrative_valid || narrative.failed) ++narrative_rejected;
					narrative_publication.reset();
				}
			});
			const auto selection_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - selection_start).count());
			selection_last_us.store(selection_us); selection_total_us.fetch_add(selection_us); ++selections;
			if (!menu_plan && !selected && !narrative.count && !progress.count && !announcement.count && !damage.count && !remote_selected && !screen_scope.count && !warnings.count && !indicator_result->count) { native(); return; }
			if (selected) ++anchors;
			s.graphics = d3d11::get_device_snapshot();
			if (!s.graphics) { native(); return; }
			if (gpu.generation != s.graphics.generation) { gpu = {}; gpu.generation = s.graphics.generation; }
			release_retired_subtractive(gpu.narrative_slots);
			for (unsigned i=0;i<weapon_scopes.size();++i)
			{
				weapon_scopes[i].graphics=s.graphics;
				weapon_scopes[i].next=i+1<weapon_scopes.size() ? &weapon_scopes[i+1] : &narrative;
			}
			narrative.graphics=subtraction.graphics=progress.graphics=announcement.graphics=warnings.graphics=damage.graphics=s.graphics;
			for(auto& layer:remote_layers)layer.graphics=s.graphics;
			for(unsigned i=0;i<screen_scopes.size();++i)
			{screen_scopes[i].graphics=s.graphics;screen_scopes[i].next=i+1<screen_scopes.size()?&screen_scopes[i+1]:&warnings;}
			narrative.next=&subtraction;subtraction.next=&progress; progress.next=&announcement; announcement.next=&damage; damage.next=&remote;remote_layers.back().next=&screen_scope;
			if (indicator_result->count)
			{
				scope atlas{}; atlas.graphics=s.graphics;
				atlas.bounds={0,0,directional_ui::atlas_size,directional_ui::atlas_size};
				const bool prepared=prepare_output(atlas,gpu.marker_slots);
				scope* tail=&warnings;
				for (unsigned i=0;i<indicator_result->count;++i)
				{
					auto& item=markers[i]; item.graphics=s.graphics; item.output=atlas.output; item.failed=!prepared;
					tail->next=&item; tail=&item;
				}
			}
			for (unsigned i=0;i<source_count;++i)
			{
				const auto& source=capture_sources[i];
				const auto index=gameplay::weapon_hud::source_index(unsigned(source.owner.holding_hand()),source.channel);
				auto& panel=weapon_scopes[i];
				panel.mirror_weapon=source.owner.holding_hand()==hand::left;
				if (selected_sources[i] && (index>=gpu.weapon_slots.size() ||
					!prepare_output(panel,independent ? gpu.weapon_slots[index] : gpu.slots)))
				{panel.count=0;panel.failed=true;++rejected;}
				if(selected_sources[i] && !panel.failed)bind_weapon_blur(panel);
			}
			menu_pass menu_capture;menu_capture.plan=menu_plan;
			if(menu_plan)prepare_menu_scopes(menu_capture,s.graphics,&s);
			active = menu_plan ? &menu_capture.scopes[0] : &s;
			const auto exit = gsl::finally([] { active = nullptr; });
			native();
			if(menu_plan)publish_menu_scopes(menu_capture);
			for (unsigned panel=0;panel<source_count;++panel)
			{
				auto& captured=weapon_scopes[panel];
				const auto& source=capture_sources[panel];
				const auto owner=source.owner;
			if (!captured.failed && captured.draws && captured.output &&
				gameplay::weapon_hud::same_presentation_owner(owner, independent ? gameplay::weapons::carry::held(owner.id()) : gameplay::weapons::current_hold()) &&
				capture_reference == controller_input::latest().reference_generation)
			{
				auto& out = *captured.output;
				out.generation = s.graphics.generation; out.sequence = ++gpu.sequence;
				out.timestamp = GetTickCount64(); out.weapon = owner.weapon;
				out.channel=source.channel;out.definition=source.definition;
				out.rear_revision = owner.rear_revision;out.instance_generation=owner.instance_generation;
				out.reference_generation = capture_reference;
				out.context = reinterpret_cast<std::uintptr_t>(s.graphics.context.Get());
				if constexpr (readback_diagnostics) if (diagnostic && !diagnostic_busy.exchange(true))
				{
					D3D11_TEXTURE2D_DESC desc{}; out.texture->GetDesc(&desc);
					desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					if (SUCCEEDED(s.graphics.device->CreateTexture2D(&desc, nullptr, &gpu.staging)))
					{
						s.graphics.context->CopyResource(gpu.staging.Get(), out.texture.Get());
						gpu.staging_width = out.width; gpu.staging_height = out.height;
						gpu.staging_at = GetTickCount64(); diagnostic_deadline.store(0);
					}
					else diagnostic_busy.store(false);
				}
				const std::lock_guard lock(publication_mutex);
				if (independent) weapon_publications[gameplay::weapon_hud::source_index(unsigned(owner.holding_hand()),source.channel)]=captured.output;
				else publication = captured.output;
				++captures;
			}
			else if (selected_sources[panel]) ++rejected;
			}
		}
		template<size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{}; mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<void*>(address), {bytes, mask.data(), N}));
		}
	}
	std::shared_ptr<const frame> latest(gameplay::weapons::weapon_identity weapon,gameplay::weapon_hud::feed channel) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		if (!weapon.weapon) return publication && publication->channel==channel ? publication : nullptr;
		auto selected=publication && publication->id()==weapon && publication->channel==channel ? publication : nullptr;
		for(const auto& frame:weapon_publications) if(frame && frame->id()==weapon && frame->channel==channel && (!selected ||
			frame->generation>selected->generation || (frame->generation==selected->generation && frame->sequence>selected->sequence))) selected=frame;
		return selected;
	}
	std::shared_ptr<const frame> latest_narrative(narrative_ui::channel channel) noexcept
	{
		const std::lock_guard lock(publication_mutex);
		switch (channel)
		{
		case narrative_ui::channel::story: return narrative_publication;
		case narrative_ui::channel::progress: return progress_publication;
		case narrative_ui::channel::announcement: return announcement_publication;
		default: return {};
		}
	}
	narrative_frames latest_narrative_layers() noexcept
	{
		const std::lock_guard lock(publication_mutex);
		return {narrative_publication,progress_publication,announcement_publication};
	}
	std::shared_ptr<const frame> latest_damage() noexcept
	{
		const std::lock_guard lock(publication_mutex); return damage_publication;
	}
	remote_frames latest_remote() noexcept
	{
		const std::lock_guard lock(publication_mutex);return remote_publication;
	}
	screen_scope_frame latest_screen_scope() noexcept
	{
		const std::lock_guard lock(publication_mutex);return screen_scope_publication;
	}
	std::shared_ptr<const indicators> latest_indicators() noexcept
	{
		const std::lock_guard lock(publication_mutex); return indicator_publication;
	}
	void set_requested(bool enabled) noexcept { requested.store(enabled); }
	counters get_counters() noexcept
	{
		return {dispatches.load(), captures.load(), copied_draws.load(), rejected.load(),
			selections.load(), selection_total_us.load(), selection_last_us.load()};
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			constexpr std::uint8_t dispatch[]{0x48,0x83,0xec,0x38,0x48,0x89,0x6c,0x24,0x50};
			constexpr std::uint8_t quad[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
			constexpr std::uint8_t text[]{0x48,0x8b,0xc4,0x48,0x89,0x68,0x10};
			constexpr std::uint8_t legacy[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
			constexpr std::uint8_t rotated[]{0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10};
			// Native dispatch table 0x1409B1C50[10] -> StretchPic handler. It reads
			// XYWH at +16..28, UV at +32..44, color at +48, then advances by size.
			constexpr std::uint8_t stretch[]{0x40,0x53,0x48,0x83,0xec,0x60,0xc7,0x44,0x24,0x50,0x06,0,0,0};
			constexpr std::uint8_t flush[]{0x33,0xc9,0xe9,0x29,0xff,0xff,0xff};
			constexpr std::uint8_t index_count[]{0xf7,0x25,0x65,0xc8,0x2c,0x11};
			command::add("vr_hud_capture_status", [] {
				console::info("[VR menu capture] global_dispatches=%llu completed=%llu failures=%llu\n",global_ui_dispatches.load(),global_ui_captures.load(),global_ui_failures.load());
				console::info("[VR UAV HUD] targets_scene/global=%llu/%llu instruments_scene/global=%llu/%llu failures=%llu selection_overflows=%llu\n",
					remote_captures[0].load(),remote_captures[1].load(),remote_captures[2].load(),remote_captures[3].load(),remote_failures.load(),remote_overflows.load());
				console::info("[VR menu capture] matched=%llu draws=%llu images=%llu empty_passes=%llu\n",menu_matches.load(),menu_draws.load(),menu_images.load(),menu_empty_passes.load());
				console::info("[VR HUD capture] requested=%d dispatches=%llu captures=%llu draws=%llu rejected=%llu\n",
					requested.load(), dispatches.load(), captures.load(), copied_draws.load(), rejected.load());
				console::info("[VR HUD capture] anchors=%llu invalid_targets=%llu unsupported_blend=%llu "
					"unsupported_depth=%llu pending=%d busy=%d\n", anchors.load(), invalid_targets.load(),
					unsupported_blend.load(), unsupported_depth.load(), diagnostic_deadline.load() != 0,
					diagnostic_busy.load());
				console::info("[VR HUD capture] black_subtractive=%llu last_blend=%u/%u/%u material=%llx color=%08x\n",
					black_subtractive_draws.load(), last_blend_src.load(), last_blend_dst.load(), last_blend_op.load(),
					last_material.load(), last_color.load());
				console::info("[VR narrative capture] captures=%llu text_draws=%llu fades=%llu rejected=%llu\n",
					narrative_captures.load(), narrative_texts.load(), narrative_fades.load(), narrative_rejected.load());
				console::info("[VR indicator capture] captures=%llu rejected=%llu\n",indicator_captures.load(),indicator_rejected.load());
			});
			if (!verify(0x14079EAE0, dispatch) || !verify(0x14079CA50, quad) || !verify(0x14079DDC0, text) ||
				!verify(0x1407B2B70, flush) || !verify(0x1407B2B0D, index_count))
			{ console::error("[VR HUD] native UI signatures rejected\n"); return; }
			dispatch_hook.create(0x14079EAE0, dispatch_stub);
			quad_hook.create(0x14079CA50, quad_stub);
			text_hook.create(0x14079DDC0, text_stub);
			if (verify(0x14079C830,legacy) && verify(0x1407A1060,rotated))
			{
				legacy_quad_hook.create(0x14079C830,legacy_quad_stub);
				rotated_quad_hook.create(0x1407A1060,rotated_quad_stub);
			}
			else console::error("[VR indicators] native legacy quad signatures rejected\n");
			if (verify(0x1407A0B30,stretch) && *reinterpret_cast<const std::uintptr_t*>(0x1409B1CA0)==0x1407A0B30)
				stretch_quad_hook.create(0x1407A0B30,stretch_quad_stub);
			else console::error("[VR indicators] native StretchPic signature/table rejected\n");
			// Opcode 25 batches 2D line vertices across LUI callbacks. Capture the
			// finalized batch through its original handler, as with text and quads.
			constexpr std::uint8_t lines[]{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01};
			if(verify(0x14079ba20,lines)&&*reinterpret_cast<const std::uintptr_t*>(0x1409b1d18)==0x14079ba20)
				lines_hook.create(0x14079ba20,lines_stub);
			else console::error("[VR menus] native line signature/table rejected\n");
			// Fixed 120-byte XY/rotated-UV quad, used by the pause-page minimap.
			// Its native handler builds the UVs and advances the original cursor.
			// Do not feed it into the 104-byte ordinary-quad decoder/read buffers.
			constexpr std::uint8_t compass[]{0x48,0x8b,0xc4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57};
			if(verify(0x14079c3e0,compass)&&*reinterpret_cast<const std::uintptr_t*>(0x1409b1ce0)==0x14079c3e0)
				compass_quad_hook.create(0x14079c3e0,compass_quad_stub);
			else console::error("[VR menus] native compass quad signature/table rejected\n");
			engine_stereo_draw_indexed::set_draw_copy_observer(copy_draw);
			// This native loop dispatches the unfiltered global UI directly through
			// the same table. Frontend menus never enter the per-view HUD dispatcher.
			constexpr std::uint8_t ui_begin[]{0x48,0x89,0x84,0x24,0xa8,0,0,0};
			constexpr std::uint8_t ui_end[]{0x8b,0x05,0x2a,0x6b,0x74,0x0e};
			if(verify(0x14079a158,ui_begin)&&verify(0x14079a1d0,ui_end))
			{
				const auto begin=utils::hook::assemble([](auto& a){emit_ui_dispatch_marker(a,reinterpret_cast<std::uintptr_t>(global_ui_begin),0x14079a160,true);});
				const auto end=utils::hook::assemble([](auto& a){emit_ui_dispatch_marker(a,reinterpret_cast<std::uintptr_t>(global_ui_end),0x14079a1d6,false,0x14079a1d6+0x0e746b2a);});
				const auto begin_relay=utils::hook::create_preserving_near_jump(0x14079a158,begin);
				const auto end_relay=utils::hook::create_preserving_near_jump(0x14079a1d0,end);
				if(begin_relay&&end_relay)
				{
					utils::hook::jump(0x14079a158,begin_relay);utils::hook::nop(0x14079a15d,3);
					utils::hook::jump(0x14079a1d0,end_relay);utils::hook::nop(0x14079a1d5,1);
				}
				else console::error("[VR menus] global UI relay allocation failed; native loop preserved\n");
			}
			else console::error("[VR menus] global UI boundary signatures rejected\n");
			if constexpr (readback_diagnostics) command::add("vr_hud_capture_once", [] {
				if (diagnostic_busy.load())
				{ console::info("[VR HUD capture] previous readback still pending\n"); return; }
				diagnostic_deadline.store(GetTickCount64() + 60000);
				arm_report();
				console::info("[VR HUD capture] armed for 60s; close console and show native ammo widget\n");
			});
		}
		void pre_destroy() override
		{
			alive.store(false); requested.store(false);
			diagnostic_deadline.store(0);
			engine_stereo_draw_indexed::set_draw_copy_observer(nullptr);
		}
	};
}
REGISTER_COMPONENT(vr::native_hud_capture::component)
