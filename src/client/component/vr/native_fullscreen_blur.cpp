#include <std_include.hpp>
#include "component/vr/native_render_contract.hpp"
#include "native_fullscreen_blur.hpp"
#include "presentation_options.hpp"
#include "native_display_backup.hpp"
#include "engine_backend_probe.hpp"
#include <utils/native_memory.hpp>
#include "component/d3d11.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::native_fullscreen_blur
{
	namespace
	{
		std::atomic_uint64_t applied{},skipped{},failures{};
		std::atomic<float> last_radius{};
		std::atomic<const char*> reason{"not requested"};
		template<std::size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
		bool contract()
		{
			static const bool valid=[] {
				constexpr std::uint8_t call[]{0xe8,0xc9,0x90,0,0};
				constexpr std::uint8_t blur[]{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x6c,0x24,0x10};
				constexpr std::uint8_t gate[]{0x66,0x83,0xb9,0xa4,0x20,0,0,6};
				constexpr std::uint8_t radius[]{0xf3,0x0f,0x10,0x82,0x34,2,0,0};
				constexpr std::uint8_t enable[]{0x4c,0x8b,0x05,0xb8,0x34,0x69,0x0e};
				constexpr std::uint8_t viewport[]{0x48,0x63,0xc2,0x48,0x8d,0x15,0x36,0x10,0x82,0x10};
				return verify(0x1407A7482,call) && verify(0x1407B0550,blur) && verify(0x1407B0E60,gate) &&
					verify(0x1407B0EAD,radius) && verify(0x1407A73E9,enable) && verify(0x1407B06C0,viewport);
			}();
			return valid;
		}
		Microsoft::WRL::ComPtr<ID3D11Texture2D> target(std::uint32_t id)
		{
			game::GfxImage* image{};ID3D11RenderTargetView* view{};
			const auto* entry=reinterpret_cast<const std::byte*>(native_render_contract::target_registry_base+
				id*native_render_contract::target_registry_stride);
			std::memcpy(&image,entry,sizeof(image));std::memcpy(&view,entry+sizeof(void*),sizeof(view));
			Microsoft::WRL::ComPtr<ID3D11Resource> resource,output;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			if(image && image->texture.shaderView)image->texture.shaderView->GetResource(&resource);
			if(view)view->GetResource(&output);
			if(!resource || resource!=output)return {};
			resource.As(&texture);
			if(!texture || image->texture.map!=texture.Get())return {};
			return texture;
		}
	}
	bool apply(void* record,native_display_contract::route route) noexcept
	{
		if(presentation_options::blur_disabled()){++skipped;reason="disabled by VR comfort setting";return true;}
		const auto reject=[](const char* why){++failures;reason=why;return false;};
		if(!record || !route || !contract())return reject("native blur contract rejected");
		float radius{};std::memcpy(&radius,static_cast<const std::byte*>(record)+0x234,sizeof(radius));
		if(!std::isfinite(radius) || radius<0)return reject("invalid native blur radius");
		last_radius=radius;
		const auto* enabled=*reinterpret_cast<const game::dvar_t* const*>(0x14EE3A8A8);
		if(!enabled || !enabled->current.enabled || !utils::hook::invoke<bool>(0x1407B0E60,record))
		{++skipped;reason="native blur inactive";return true;}
		try
		{
			const auto graphics=d3d11::get_device_snapshot();
			const auto raw=target(route.source),display=target(route.destination);
			if(!graphics || !raw || !display)return reject("native blur targets unavailable");
			// One GPU-only backup per renderer thread. No readback, GPU wait or
			// registry replacement. Queued copies bracket each eye independently.
			thread_local native_display_contract::source_backup backup;
			std::array<short,4> viewport{};
			utils::hook::invoke<void>(0x1407B06C0,record,route.source,viewport.data());
			if(viewport[2]<=0 || viewport[3]<=0)return reject("native blur viewport rejected");
			// H2's own Gaussian chain writes its scratch target 24, then blends
			// into the explicit destination. Source and destination must differ.
			// The original tail uses 13 -> 1; VR uses display -> preserved HDR slot.
			if(!backup.compose(graphics.context.Get(),raw.Get(),display.Get(),[&]{
				utils::hook::invoke<void>(0x1407B0550,record,viewport.data(),true,route.destination,route.source);return true;
			}))return reject("native blur source preservation failed");
			++applied;reason="native fullscreen blur applied per eye";return true;
		}
		catch(...){return reject("native fullscreen blur exception");}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			command::add("vr_death_effect_status",[]{
				console::info("[VR native blur] applied=%llu skipped=%llu failures=%llu radius=%.4f reason=%s\n",
					applied.load(),skipped.load(),failures.load(),last_radius.load(),reason.load());
				console::info("[VR blur sources] %s",source_status().c_str());
			});
		}
	};
}
REGISTER_COMPONENT(vr::native_fullscreen_blur::component)
