#include <std_include.hpp>
#include "scripted_camera_reference.hpp"
#include <utils/native_memory.hpp>
#include "../pose_filter.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::sequences::camera_reference
{
    namespace
    {
        std::atomic_uint64_t samples{},misses{},source_generation{};
        std::atomic<const char*> reason{"no authored rotation requested"};
        template<class T>bool read(const void* p,std::size_t offset,T& out)
        {return p && utils::native_memory::read_bytes(&out,static_cast<const std::byte*>(p)+offset,sizeof(out));}
        template<std::size_t N>bool verify(std::uintptr_t address,const std::array<std::uint8_t,N>& bytes)
        {std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes.data(),mask.data(),N}));}
        bool ready()
        {
            // The same H2 client object/tag-matrix ABI used by mounted turret
            // camera tags, verified by a bounded read on 2026-09-30.
            static const bool valid=verify(0x1405A6DD0,std::array<std::uint8_t,16>{0x48,0x63,0xc1,0x48,0x8d,0x0d,0xa6,0xc2,0xb6,0x0a,0x0f,0xbf,0x0c,0x41,0x85,0xc9}) &&
                verify(0x140370E10,std::array<std::uint8_t,12>{0x40,0x53,0x48,0x81,0xec,0x90,0,0,0,0x49,0x8b,0xd9});
            return valid;
        }
        struct tag_cache
        {
            const void* object{};std::uint64_t reference{},source{};
            game_view::scripted_camera_tag kind{};unsigned count{},tag{};
            std::array<game::XModel*,32> models{};
        };
        thread_local tag_cache cached;
    }
    game_view::scripted_rotation_reference sample(const view& v)noexcept
    {
        if(!v.camera.needs_tag())return {};
        const auto reject=[](const char* text){reason=text;++misses;return game_view::scripted_rotation_reference{};};
        if(!v.epoch || !v.position_epoch || v.linked_entity<=0 || v.linked_entity>=4000 ||
            v.rotation_tag==game_view::scripted_camera_tag::none || !game::CL_IsCgameInitialized())return reject("invalid linked camera identity");
        if(!ready())return reject("native camera tag contract rejected");
        auto* centity=reinterpret_cast<void*>(0x141C328F0ull+std::uintptr_t(v.linked_entity)*0x200);
        unsigned short entity{};if(!read(centity,0x1b4,entity) || entity!=v.linked_entity)return reject("client camera entity mismatch");
        auto* object=utils::hook::invoke<void*>(0x1405A6DD0,v.linked_entity,0);
        unsigned char count{};game::XModel** model_array{};std::array<game::XModel*,32> models{};
        if(!read(object,15,count) || !count || count>models.size() || !read(object,0xd8,model_array) ||
            !utils::native_memory::read_bytes(models.data(),model_array,count*sizeof(models[0])))return reject("client camera model unavailable");
        if(cached.object!=object || cached.reference!=v.position_epoch || cached.kind!=v.rotation_tag || cached.count!=count || cached.models!=models)
        {
            tag_cache next{object,v.position_epoch,++source_generation,v.rotation_tag,count,0,models};unsigned total{};
            const char* wanted=game_view::name(v.rotation_tag);
            for(unsigned m=0;m<count;++m)
            {
                game::XModel model{};if(!read(models[m],0,model))return reject("camera model header unavailable");
                if(!model.numBones)continue;
                if(!model.boneNames || total+model.numBones>256)return reject("camera skeleton bound rejected");
                total+=model.numBones;std::array<game::scr_string_t,256> names{};
                if(!utils::native_memory::read_bytes(names.data(),model.boneNames,model.numBones*sizeof(names[0])))return reject("camera bone names unavailable");
                for(unsigned b=0;b<model.numBones;++b)
                {
                    if(names[b]<=0 || unsigned(names[b])>=0x40000)continue;
                    const auto* name=game::SL_ConvertToString(names[b]);
                    if(name && !_stricmp(name,wanted))next.tag=unsigned(names[b]);
                }
            }
            cached=next;
        }
        if(!cached.tag)return reject("authored camera tag missing");
        game_view::scripted_rotation_reference out;std::array<float,3> origin{};
        if(!utils::hook::invoke<int>(0x140370E10,centity,object,cached.tag,out.axis.data(),origin.data()))return reject("authored camera pose unavailable");
        if(utils::hook::invoke<void*>(0x1405A6DD0,v.linked_entity,0)!=object)return reject("camera object changed during sample");
        if(!pose_filter::valid({{},out.axis}))return reject("invalid authored camera basis");
        out.source=cached.source;++samples;reason="authored camera tag sampled";return out;
    }
    std::string status()
    {return std::format("authored_yaw_samples={} misses={} reason={}\n",samples.load(),misses.load(),reason.load());}
}
