#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <cstring>

namespace vr::diagnostics::screen
{
	enum gate : std::uint64_t
	{
		owner=1ull<<0, epoch=1ull<<1, origins=1ull<<2, tracking=1ull<<3,
		reference=1ull<<4, recenter=1ull<<5, world_scale=1ull<<6,
		hud_missing=1ull<<7, hud_view=1ull<<8, hud_scope_epoch=1ull<<9,
		hud_display_epoch=1ull<<10, hud_weapon=1ull<<11, hud_revision=1ull<<12,
		hud_device=1ull<<13, hud_age=1ull<<14, hud_reference=1ull<<15,
		hud_context=1ull<<16, canvas_aspect=1ull<<17, plane=1ull<<18,
		projection=1ull<<19, window=1ull<<20, pair=1ull<<21, publication=1ull<<22,
		device=1ull<<23, auxiliary_missing=1ull<<24, auxiliary_image=1ull<<25,
		auxiliary_owner=1ull<<26, auxiliary_generation=1ull<<27, auxiliary_reference=1ull<<28,
		auxiliary_mode=1ull<<29, composition=1ull<<30, exception=1ull<<31,
		hud_parse=1ull<<32, hud_border=1ull<<33, hud_draws=1ull<<34,
		hud_layers=1ull<<35, auxiliary_record=1ull<<36, auxiliary_copy=1ull<<37,
		hud_blend=1ull<<38,hud_depth=1ull<<39,hud_target=1ull<<40,hud_output=1ull<<41
	};
	inline void check(std::uint64_t& mask,bool valid,gate flag) noexcept {if(!valid)mask|=flag;}
	struct sample
	{
		const char* stage{"not_observed"};
		const char* consumer{"unknown"};
		const char* gpu_stage{"not_called"};
		const char* shader{"none"};
		std::int64_t gpu_result{};
		bool gpu_result_known{};
		bool active{},success{},hud_retained{},auxiliary_valid{},auxiliary_has_image{},hud_producer_enabled{};
		bool hud_producer_sampled{};
		std::uint64_t tick{},pair_id{},publication_id{},epoch_id{},owner_epoch{},device_id{},reference_id{};
		std::uint64_t expected_pair{},expected_publication{},expected_device{};
		std::uint64_t rejected{},hud_rejected{},owner_id{},owner_generation{},owner_revision{};
		std::uint64_t hud_sequence{},hud_tick{},hud_scope{},hud_display{},hud_device_id{},hud_reference_id{};
		std::uint64_t auxiliary_owner_id{},auxiliary_generation_id{},auxiliary_reference_id{},auxiliary_revision{};
		std::uintptr_t context{},hud_context_id{};
		std::uint32_t eye{2},hud_width{},hud_height{},layer_mask{};
		std::array<std::uint32_t,3> commands{},draws{};
		std::array<std::uint32_t,3> blend_src{},blend_dst{},blend_op{};
		std::array<std::array<char,64>,8> materials{};
		std::uint32_t material_count{},material_overflow{};
		std::array<float,4> crop{};
		float native_half_x{},native_half_y{},near_distance{},canvas_min_w{},canvas_max_w{};
	};
	inline std::string gates(std::uint64_t mask)
	{
		constexpr std::array names{"owner","epoch","model_origins","tracking","reference","recenter","world_scale",
			"hud_missing","hud_srv","hud_scope_epoch","hud_display_epoch","hud_weapon","hud_revision","hud_device",
			"hud_age","hud_reference","hud_context","canvas_aspect","plane","projection","crop_window","pair",
			"publication","device","auxiliary_missing","auxiliary_image","auxiliary_owner","auxiliary_generation",
			"auxiliary_reference","auxiliary_mode","composition","exception","hud_parse","hud_border","hud_draws",
			"hud_layers","auxiliary_record","auxiliary_copy","hud_blend","hud_depth","hud_target","hud_output"};
		std::string text;
		for(std::size_t i{};i<names.size();++i)if(mask&(1ull<<i)){if(!text.empty())text+='|';text+=names[i];}
		return text.empty()?"none":text;
	}
	class trace
	{
		std::mutex mutex_;
		sample latest_,first_failure_,last_failure_,last_success_;
		std::atomic_uint64_t observations_{},active_{},successes_{},failures_{},dropped_{};
	public:
		void record(const sample& value) noexcept
		{
			++observations_;if(value.active)++active_;if(value.success)++successes_;
			const bool failed=value.active&&value.rejected;
			if(failed)++failures_;
			const std::unique_lock lock(mutex_,std::try_to_lock);
			if(!lock.owns_lock()){++dropped_;return;}
			latest_=value;
			if(value.success)last_success_=value;
			if(failed){if(!first_failure_.rejected)first_failure_=value;last_failure_=value;}
		}
		std::string format(const char* name,std::uint64_t now)
		{
			sample latest,first,last,success;bool ready{};
			{const std::unique_lock lock(mutex_,std::try_to_lock);ready=lock.owns_lock();if(ready){latest=latest_;first=first_failure_;last=last_failure_;success=last_success_;}}
			std::ostringstream out;
			out<<"[VR "<<name<<"] observations="<<observations_.load()<<" active_events="<<active_.load()
				<<" success_events="<<successes_.load()<<" rejected_events="<<failures_.load()
				<<" dropped_samples="<<dropped_.load()<<" snapshot_busy="<<!ready<<'\n';
			if(!ready)return out.str();
			const auto age=[now](std::uint64_t tick)->std::int64_t{return tick&&now>=tick?static_cast<std::int64_t>(now-tick):-1;};
			const auto append=[&](const char* which,const sample& s)
			{
				out<<"  "<<which<<": consumer="<<s.consumer<<" stage="<<s.stage<<" active="<<s.active<<" success="<<s.success<<" age_ms="<<age(s.tick)
					<<" rejected="<<gates(s.rejected)<<" hud_rejected="<<gates(s.hud_rejected)
					<<" pair="<<s.pair_id<<" publication="<<s.publication_id<<" eye="<<s.eye
					<<" epoch="<<s.epoch_id<<" owner_epoch="<<s.owner_epoch<<" device="<<s.device_id<<" reference="<<s.reference_id<<'\n';
				out<<"    expected_pair/publication/device="<<s.expected_pair<<'/'<<s.expected_publication<<'/'<<s.expected_device<<'\n';
				out<<"    screen_compositor_stage="<<s.gpu_stage<<" shader="<<s.shader<<" result_known="<<s.gpu_result_known<<" result="<<s.gpu_result<<'\n';
				out<<"    owner="<<s.owner_id<<':'<<s.owner_generation<<':'<<s.owner_revision
					<<" hud="<<s.hud_sequence<<" hud_age_ms="<<age(s.hud_tick)<<" hud_epochs="<<s.hud_scope<<'/'<<s.hud_display
					<<" hud_device/reference="<<s.hud_device_id<<'/'<<s.hud_reference_id<<" hud_size="<<s.hud_width<<'x'<<s.hud_height
					<<" hud_retained="<<s.hud_retained<<" layer_mask="<<s.layer_mask
					<<" hud_producer_enabled="<<(s.hud_producer_sampled?(s.hud_producer_enabled?"yes":"no"):"unobserved")
					<<" context=0x"<<std::hex<<s.context<<" hud_context=0x"<<s.hud_context_id<<std::dec<<'\n';
				out<<"    auxiliary="<<s.auxiliary_valid<<" image="<<s.auxiliary_has_image<<" identity="<<s.auxiliary_owner_id<<':'
					<<s.auxiliary_generation_id<<':'<<s.auxiliary_reference_id<<':'<<s.auxiliary_revision
					<<" crop="<<s.crop[0]<<','<<s.crop[1]<<','<<s.crop[2]<<','<<s.crop[3]
					<<" native_half="<<s.native_half_x<<','<<s.native_half_y<<" near="<<s.near_distance
					<<" canvas_w="<<s.canvas_min_w<<','<<s.canvas_max_w
					<<" commands="<<s.commands[0]<<'/'<<s.commands[1]<<'/'<<s.commands[2]
					<<" draws="<<s.draws[0]<<'/'<<s.draws[1]<<'/'<<s.draws[2]<<'\n';
				if(s.material_count)
				{
					out<<"    material_candidates: overflow="<<s.material_overflow;
					for(std::size_t i{};i<s.material_count&&i<s.materials.size();++i)out<<' '<<s.materials[i].data();
					out<<" blend(src/dst/op)=";
					for(unsigned i{};i<3;++i)out<<'['<<i<<':'<<s.blend_src[i]<<'/'<<s.blend_dst[i]<<'/'<<s.blend_op[i]<<']';
					out<<'\n';
				}
			};
			append("latest",latest);if(first.rejected)append("first_failure",first);if(last.rejected)append("last_failure",last);if(success.tick)append("last_success",success);
			return out.str();
		}
	};
	inline trace fixed_scope,javelin_scope,fixed_hud,javelin_hud,auxiliary;
	template<class Status> void draw_result(sample& value,const Status& status) noexcept
	{value.gpu_stage=status.stage;value.shader=status.shader;value.gpu_result=status.result;value.gpu_result_known=status.result_known;}
	inline void note_material(sample& s,std::string_view name) noexcept
	{
		if(name.find("javelin")==name.npos&&name.find("sniper")==name.npos&&name.find("barret")==name.npos&&
			name.find("scope")==name.npos&&name.find("thermal")==name.npos&&name.find("reticle")==name.npos)return;
		name=name.substr(0,63);
		for(std::size_t i{};i<s.material_count;++i)if(name==s.materials[i].data())return;
		if(s.material_count>=s.materials.size()){++s.material_overflow;return;}
		std::memcpy(s.materials[s.material_count++].data(),name.data(),name.size());
	}
	// Copy CPU lease metadata only; never query D3D objects during diagnostics.
	template<class Frame> void hud(sample& s,const Frame* frame) noexcept
	{
		if(!frame)return;
		s.hud_sequence=frame->sequence;s.hud_tick=frame->timestamp;s.hud_scope=frame->screen_scope_epoch;
		s.hud_display=frame->weapon_display_epoch;s.hud_device_id=frame->generation;s.hud_reference_id=frame->reference_generation;
		s.hud_width=frame->width;s.hud_height=frame->height;s.hud_context_id=frame->context;
	}
	template<class Event> sample observation(const Event& event,std::uint64_t epoch,std::uint64_t tick) noexcept
	{
		sample s;s.tick=tick;s.active=epoch!=0;s.epoch_id=epoch;s.pair_id=event.pair_id;
		s.consumer=event.views.screen_scope_epoch?"fixed_scope":event.views.weapon_display_epoch?"javelin":"optic";
		s.publication_id=event.views.eyes[event.eye<2?event.eye:0].publication;s.eye=event.eye;s.device_id=event.device_generation;
		s.native_half_x=event.views.native_tan_half[0];s.native_half_y=event.views.native_tan_half[1];s.near_distance=event.views.native_near_distance;
		if(event.auxiliary){const auto& a=*event.auxiliary;s.auxiliary_valid=a.valid;s.auxiliary_owner_id=a.owner;
			s.auxiliary_generation_id=a.generation;s.auxiliary_reference_id=a.reference;s.auxiliary_revision=a.revision;s.crop=a.window;}
		s.auxiliary_has_image=event.auxiliary_image!=nullptr;return s;
	}
}
