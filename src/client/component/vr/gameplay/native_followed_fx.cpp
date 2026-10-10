#include <std_include.hpp>
#include "native_followed_fx.hpp"
#include "followed_fx_policy.hpp"
#include "native_weapon_fx.hpp"
#include "native_fx_checkpoint.hpp"
#include <utils/native_memory.hpp>
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::native_followed_fx
{
    namespace
    {
        constexpr std::uintptr_t spawn_address=0x1404547C0,update_address=0x14045E5E0,stop_address=0x140458A70;
        struct entry
        {
            game::FxEffectDef definition{};std::vector<game::FxElemDef> elements;
            std::mutex mutex;state current;std::uint64_t generation{};
            game::FxEffectDef* source{};unsigned attached_mask{};std::string checkpoint_name;
        };
        std::array<std::unique_ptr<entry>,8> storage;
        std::array<std::atomic<entry*>,8> published{};
        std::uint64_t generation{};bool ready{};
        utils::hook::detour spawn_hook,update_hook;
        entry* get(handle h) noexcept
        {
            if(!h)return nullptr;auto* p=published[h.slot].load(std::memory_order_acquire);
            return p && p->generation==h.generation?p:nullptr;
        }
        entry* owner(const void* definition) noexcept
        {
            for(auto& slot:published)if(auto* p=slot.load(std::memory_order_acquire);p && &p->definition==definition)return p;
            return nullptr;
        }
        template<class T>T read(const void* p,std::size_t offset)
        {T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;}
        game::FxSpatialFrame frame(hands::anchor pose)
        {
            game::FxSpatialFrame out{};std::copy(pose.rotation.begin(),pose.rotation.end(),out.quat);
            std::copy(pose.position.begin(),pose.position.end(),out.origin);return out;
        }
        void* spawn(void* system,int mode,const void* request)
        {
            auto* p=owner(read<const void*>(request,0));
            if(!p)return spawn_hook.invoke<void*>(system,mode,request);
            state s;{const std::lock_guard lock(p->mutex);s=p->current;}
            if(!s.follows(read<int>(request,0x4c)))return nullptr;
            // Native queue -> spawn copies a 0xe0-byte descriptor. A private
            // stack copy preserves its links/options while refreshing only pose.
            alignas(16) std::array<std::byte,0xe0> copy{};std::memcpy(copy.data(),request,copy.size());
            const auto pose=frame(s.pose);std::memcpy(copy.data()+8,&pose,sizeof(pose));
            return spawn_hook.invoke<void*>(system,mode,copy.data());
        }
        void update(void* system,int mode,void* effect,unsigned native_handle)
        {
            if(auto* p=owner(read<const void*>(effect,0)))
            {
                state s;{const std::lock_guard lock(p->mutex);s=p->current;}
                if(s.follows(read<int>(effect,0x5c)))
                {
                    // Only the native FX simulation owner writes frameNow.
                    // Native update then advances particles and copies it to
                    // framePrev; render/eye callbacks only publish value poses.
                    const auto pose=frame(s.pose);std::memcpy(static_cast<std::byte*>(effect)+0x84,&pose,sizeof(pose));
                }
                else if(read<unsigned>(effect,8)&0x08004000u)
                    utils::hook::invoke<void>(stop_address,system,effect); // Stop spawning; native tails drain normally.
            }
            update_hook.invoke<void>(system,mode,effect,native_handle);
        }
        template<std::size_t N>bool verify(std::uintptr_t at,const std::uint8_t (&bytes)[N])
        {
            std::array<std::uint8_t,N> actual{};
            return utils::native_memory::read_bytes(actual.data(),reinterpret_cast<const void*>(at),N) &&
                std::equal(actual.begin(),actual.end(),bytes);
        }
        handle definition(game::FxEffectDef* source,unsigned mask)
        {
            if(!source || !source->name)return {};
            const auto count=std::int64_t(source->elemDefCountLooping)+source->elemDefCountEmission+source->elemDefCountOneShot;
            if(count<=0 || count>32 || source->elemDefCountLooping<0 || source->elemDefCountEmission<0 ||
                source->elemDefCountOneShot<0 || !source->elemDefs || source->msecLoopingLife<=0 || source->msecLoopingLife>60000 ||
                (count<32 && (mask>>count)))return {};
            for(unsigned i=0;i<storage.size();++i)if(storage[i] && storage[i]->source==source && storage[i]->attached_mask==mask)
                return {i,storage[i]->generation};
            for(unsigned i=0;i<storage.size();++i)if(!storage[i])
            {
                auto p=std::make_unique<entry>();p->source=source;p->attached_mask=mask;
                p->checkpoint_name=native_fx::checkpoint::name(native_fx::checkpoint::variant::followed,
                    {source->name,strnlen_s(source->name,native_fx::checkpoint::source_name_limit+1)},mask);
                if(p->checkpoint_name.empty())return {};
                p->definition=*source;p->elements.assign(source->elemDefs,source->elemDefs+count);
                for(unsigned n=0;n<unsigned(count);++n)if(mask&(1u<<n))
                    p->elements[n].flags=(p->elements[n].flags&~game::FX_ELEM_RUN_MASK)|game::FX_ELEM_RUN_RELATIVE_TO_EFFECT;
                p->definition.elemDefs=p->elements.data();p->definition.name=p->checkpoint_name.c_str();p->generation=++generation;
                const std::array<game::FxEffectDef*,1> definitions{&p->definition};
                if(!native_fx::checkpoint::publish(definitions))return {};
                storage[i]=std::move(p);published[i].store(storage[i].get(),std::memory_order_release);return {i,generation};
            }
            return {};
        }
    }
    bool initialize() noexcept
    {
        if(ready)return true;
        // Read-only H2 witness: spawn descriptor frames -> effect 68/84/a0;
        // 45E5E0 consumes frameNow 84 and copies it to framePrev a0 after update.
        constexpr std::uint8_t spawn_entry[]{0x40,0x53,0x56,0x41,0x54,0x41,0x56,0x48,0x83,0xec,0x78};
        constexpr std::uint8_t update_entry[]{0x4c,0x8b,0xdc,0x53,0x57,0x41,0x54,0x41,0x56,0x48,0x81,0xec,0xc8,0,0,0};
        constexpr std::uint8_t now_write[]{0x0f,0x11,0x87,0x84,0,0,0};
        constexpr std::uint8_t previous_write[]{0x0f,0x11,0x83,0xa0,0,0,0};
        constexpr std::uint8_t stop_entry[]{0x48,0x89,0x5c,0x24,0x20,0x56,0x48,0x83,0xec,0x20};
        // H2 differs from the inherited FX flag names: 0x40 selects r9's
        // interpolated effect frame, while 0x80 reads the FxCamera at stack+0xa0.
        // A wrong enum here pins attached flame/light elements to both eyes.
        constexpr std::uint8_t effect_frame[]{0x83,0xf8,0x40,0x75,0x21,0x41,0x8b,0x41,0x10,0x49,0x8b,0xc9};
        constexpr std::uint8_t camera_frame[]{0x3d,0x80,0,0,0,0x75,0x34,0x48,0x8b,0x8c,0x24,0xa0,0,0,0};
        static_assert(game::FX_ELEM_RUN_RELATIVE_TO_EFFECT==0x40 && game::FX_ELEM_RUN_RELATIVE_TO_CAMERA==0x80);
        if(!verify(spawn_address,spawn_entry) || !verify(update_address,update_entry) ||
            !verify(0x140454BAB,now_write) || !verify(0x14045E832,previous_write) ||
            !verify(0x140462819,effect_frame) || !verify(0x14046278F,camera_frame) ||
            !verify(stop_address,stop_entry) || !weapons::native_weapon_fx::initialize())return false;
        static_assert(sizeof(game::FxSpatialFrame)==28);
        spawn_hook.create(spawn_address,spawn);update_hook.create(update_address,update);
        fastfiles::on_pre_unload([] {
            // Native DB unload is already drained, as for rigid-part/DObj owners.
            native_fx::checkpoint::remove(native_fx::checkpoint::variant::followed);
            for(auto& p:published)p.store(nullptr,std::memory_order_release);
            for(auto& p:storage)p.reset();
        });
        ready=true;return true;
    }
    handle create(game::FxEffectDef* source,std::span<const unsigned> attached_elements)
    {
        if(!ready || !source || !scheduler::is_executing(scheduler::pipeline::main))return {};
        unsigned mask{};
        for(auto n:attached_elements){if(n>=32)return {};mask|=1u<<n;}
        return definition(source,mask);
    }
    game::FxEffectDef* restore_definition(game::FxEffectDef* source,unsigned mask)
    {
        if(!initialize())return nullptr;
        const auto h=definition(source,mask);
        auto* p=get(h);return p?&p->definition:nullptr;
    }
    bool start(handle h,std::uint64_t activation,int time,hands::anchor pose)
    {
        auto* p=get(h);if(!p || !activation || time<0 || !valid_pose(pose) || !scheduler::is_executing(scheduler::pipeline::main))return false;
        {const std::lock_guard lock(p->mutex);if(p->current.activation==activation)return true;p->current.begin(activation,time,pose);}
        return weapons::native_weapon_fx::play_frontend(&p->definition,pose,time);
    }
    void position(handle h,std::uint64_t activation,hands::anchor pose) noexcept
    {
        auto* p=get(h);if(!p)return;const std::lock_guard lock(p->mutex);p->current.position(activation,pose);
    }
    void stop(handle h) noexcept
    {if(auto* p=get(h)){const std::lock_guard lock(p->mutex);p->current.active=false;}}
}
