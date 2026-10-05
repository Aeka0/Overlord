#include <std_include.hpp>
#include "native_caption_font.hpp"
#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::native_caption_font
{
	namespace
	{
		std::mutex mutex;std::shared_ptr<const face> loaded;
		void retire(){const std::lock_guard lock(mutex);loaded.reset();}
		void prepare()
		{
			{const std::lock_guard lock(mutex);if(loaded)return;}
			if(!game::CL_IsCgameInitialized())return;
			const auto* asset=game::DB_FindXAssetHeader(game::ASSET_TYPE_TTF,"fonts/bank.ttf",0).ttfDef;
			if(!asset || !asset->name || std::string_view(asset->name)!="fonts/bank.ttf" || !asset->file || asset->fileLen<=0 || asset->fileLen>4*1024*1024)return;
			auto result=make_bank_face({reinterpret_cast<const std::byte*>(asset->file),size_t(asset->fileLen)});
			const std::lock_guard lock(mutex);loaded=std::move(result);
		}
		class component final:public component_interface
		{
			void post_unpack()override {scheduler::loop(prepare,scheduler::pipeline::main,1s);fastfiles::on_pre_unload(retire);}
		};
	}
	std::shared_ptr<const face> bank() noexcept {const std::lock_guard lock(mutex);return loaded;}
}
REGISTER_COMPONENT(vr::native_caption_font::component)
