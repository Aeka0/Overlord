#include <std_include.hpp>
#include "attachment_pose.hpp"
#include "../../engine_stereo_view.hpp"
#include <utils/native_memory.hpp>
#include <mutex>

namespace vr::gameplay::hands::attachments
{
	namespace
	{
		std::mutex mutex;
		std::array<solved,128> poses{};std::size_t cursor{};
		struct prepared {const void* record{};std::array<float,12> camera{};solved pose{};};
		std::array<prepared,128> records{};std::size_t record_cursor{};
		bool matches(const solved& value) noexcept
		{
			std::uint32_t epoch{};
			if (!value.object || !value.matrices || !utils::native_memory::read_bytes(&epoch,reinterpret_cast<const std::byte*>(value.object)+0xb0,sizeof(epoch)) || epoch!=value.epoch) return false;
			for (unsigned h=0;h<2;++h)
			{
				bone current;
				if (value.indices[h]>=256 || !utils::native_memory::read_bytes(&current,reinterpret_cast<const bone*>(value.matrices)+value.indices[h],sizeof(current)) ||
					std::memcmp(&current,&value.wrists[h],sizeof(current))) return false;
			}
			return true;
		}
	}
	void publish(const solved& value) noexcept
	{
		if (!value.object || !value.matrices || !value.reference || !value.sequence) return;
		const std::lock_guard lock(mutex);poses[cursor++%poses.size()]=value;
	}
	void begin_record(const void* record) noexcept
	{
		const std::lock_guard lock(mutex);
		for (auto& previous:records) if (previous.record==record) previous={};
	}
	solved before_skin(const void* object,const bone* matrices) noexcept
	{
		solved found;
		{const std::lock_guard lock(mutex);for (std::size_t n=0;n<poses.size();++n)
			{const auto& p=poses[(cursor+poses.size()-1-n)%poses.size()];if (p.object==reinterpret_cast<std::uintptr_t>(object) && p.matrices==reinterpret_cast<std::uintptr_t>(matrices)){found=p;break;}}}
		return matches(found) ? found : solved{};
	}
	void after_skin(const solved& value,int result,const void* record) noexcept
	{
		if (result<=0 || !record || !matches(value)) return;
		prepared out;out.record=record;out.pose=value;
		if (!utils::native_memory::read_bytes(out.camera.data(),static_cast<const std::byte*>(record)+engine_stereo_view::h2_view_origin_offset,sizeof(out.camera))) return;
		const std::lock_guard lock(mutex);records[record_cursor++%records.size()]=out;
	}
	bool for_record(const void* record,const std::array<float,12>& camera,solved& out,int reload_hand,std::uint64_t reload_revision) noexcept
	{
		if(reload_hand < -1 || reload_hand>1 || (reload_hand>=0 && !reload_revision))return false;
		const auto now=controller_input::clock::now();const std::lock_guard lock(mutex);
		for (std::size_t n=0;n<records.size();++n)
		{
			const auto& p=records[(record_cursor+records.size()-1-n)%records.size()];
			if (p.record==record && p.camera==camera && now>=p.pose.at && now-p.pose.at<=std::chrono::milliseconds(150) &&
				(reload_hand<0 || p.pose.reload_items[reload_hand]==reload_revision)){out=p.pose;return true;}
		}
		return false;
	}
	void clear_after_drain() noexcept {const std::lock_guard lock(mutex);poses={};records={};cursor=record_cursor=0;}
}
