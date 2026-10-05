#pragma once
#include "rig_builder.hpp"
#include "rig_cache.hpp"
#include "game/game.hpp"
#include <cstring>

namespace vr::gameplay::hands
{
    struct native_object
    {
        void* tree;
        std::uint32_t duplicate_parts;
        std::byte pad_header[3];
        std::uint8_t model_count,bone_count;
        std::byte pad1[111];
        std::uint32_t calculated[8];
        std::byte pad2[8];
        bone* matrices;
        std::uint32_t timestamp;
        std::byte pad3[36];
        game::XModel** models;
    };
    static_assert(offsetof(native_object,duplicate_parts)==8);
    static_assert(offsetof(native_object,calculated)==0x80);
    static_assert(offsetof(native_object,matrices)==0xa8);
    static_assert(offsetof(native_object,timestamp)==0xb0);
    static_assert(offsetof(native_object,models)==0xd8);
    static_assert(sizeof(bone)==sizeof(game::DObjAnimMat));

    // Caller holds the native DObj lock, as for describe(). Asset-owned strings,
    // material/surface contents and script-string aliases are immutable until
    // the native asset-unload barrier. The asset epoch rejects address reuse;
    // linear bone copies also detect in-place bind/topology edits without the
    // repeated string conversion and pose-library matching done by describe().
    inline bool binding_identity(const native_object& object,std::uint64_t resource,std::uint64_t assets,
        unsigned admission,bool empty,rig_binding_identity& out) noexcept
    {
        out={};out.object=reinterpret_cast<std::uintptr_t>(&object);
        out.model_array=reinterpret_cast<std::uintptr_t>(object.models);
        out.resource=resource;out.assets=assets;out.admission=admission;out.empty=empty;
        out.duplicate_parts=object.duplicate_parts;out.model_count=object.model_count;out.bone_count=object.bone_count;
        if(!object.models || !object.model_count || object.model_count>out.models.size() || !object.bone_count)return false;
        const auto* attachments=reinterpret_cast<const std::uint8_t*>(object.models+object.model_count);
        unsigned offset{};
        for(unsigned m=0;m<object.model_count;++m)
        {
            const auto* model=object.models[m];if(!model)return false;
            auto& entry=out.models[m];entry.model=reinterpret_cast<std::uintptr_t>(model);
            entry.name=reinterpret_cast<std::uintptr_t>(model->name);
            entry.bone_names=reinterpret_cast<std::uintptr_t>(model->boneNames);
            entry.parents=reinterpret_cast<std::uintptr_t>(model->parentList);
            entry.bind=reinterpret_cast<std::uintptr_t>(model->baseMat);
            entry.bone_info=reinterpret_cast<std::uintptr_t>(model->boneInfo);
            entry.materials=reinterpret_cast<std::uintptr_t>(model->materialHandles);
            entry.surfaces=reinterpret_cast<std::uintptr_t>(model->lodInfo[0].surfs);
            std::memcpy(entry.bounds.data(),&model->bounds,sizeof(model->bounds));
            entry.bones=model->numBones;entry.roots=model->numRootBones;entry.surface_count=model->numsurfs;
            entry.lods=model->numLods;entry.lod_surfaces=model->lodInfo[0].numsurfs;entry.lod_offset=model->lodInfo[0].surfIndex;
            entry.attachment=attachments[m];
            if(!model->name || !model->numBones || !model->boneNames || !model->numRootBones ||
                model->numRootBones>model->numBones || (model->numBones>model->numRootBones && !model->parentList) ||
                model->numBones>object.bone_count-offset)return false;
            for(unsigned b=0;b<model->numBones;++b)
            {
                auto& joint=out.bones[offset+b];joint.name=model->boneNames[b];
                if(b>=model->numRootBones)joint.parent_distance=model->parentList[b-model->numRootBones];
                if(model->baseMat)
                {
                    std::memcpy(joint.bind.data(),model->baseMat[b].quat,4*sizeof(float));
                    std::memcpy(joint.bind.data()+4,model->baseMat[b].trans,3*sizeof(float));
                }
            }
            offset+=model->numBones;
        }
        return offset==object.bone_count;
    }

    // Native DObj attachment parents and duplicate aliases are part of the rig,
    // including authored props parented to body wrists. Caller holds DObj lock.
    inline bool describe(const native_object& object,std::array<model_definition,32>& models,
        std::array<bone_definition,256>& bones) noexcept
    {
        if(!object.models || !object.model_count || object.model_count>models.size() || !object.bone_count)return false;
        const auto* attachments=reinterpret_cast<const std::uint8_t*>(object.models+object.model_count);
        int offset{};
        for(int m=0;m<object.model_count;++m)
        {
            const auto* model=object.models[m];
            if(!model || !model->name || !model->numBones || !model->boneNames || !model->numRootBones ||
                model->numRootBones>model->numBones || (model->numBones>model->numRootBones && !model->parentList) ||
                model->numBones>object.bone_count-offset)return false;
            const auto length=strnlen_s(model->name,256);if(length==256)return false;
            models[m]={{model->name,length},offset,model->numBones};
            for(int i=0;i<model->numBones;++i)
            {
                const int index=offset+i;
                const char* name=game::SL_ConvertToString(model->boneNames[i]);if(!name)return false;
                const auto size=strnlen_s(name,128);if(size==128)return false;
                int parent=attachments[m]==255 ? -1 : attachments[m];
                if(i>=model->numRootBones)
                {
                    const int distance=model->parentList[i-model->numRootBones];
                    if(distance<=0 || distance>i)return false;
                    parent=index-distance;
                }
                bones[index]={{name,size},parent};
                if(model->baseMat)
                {
                    std::copy_n(model->baseMat[i].quat,4,bones[index].bind.rotation.begin());
                    std::copy_n(model->baseMat[i].trans,3,bones[index].bind.position.begin());
                }
            }
            offset+=model->numBones;
        }
        if(offset!=object.bone_count)return false;
        const auto* duplicate=reinterpret_cast<const std::uint8_t*>(game::SL_ConvertToString(object.duplicate_parts));
        if(!duplicate)return false;
        std::array<bool,256> seen{};
        for(int pairs=0;pairs<object.bone_count;++pairs)
        {
            const int destination=duplicate[32+pairs*2]-1;
            if(destination<0)return true;
            const int source=duplicate[33+pairs*2]-1;
            if(destination>=object.bone_count || source<0 || source>=destination || seen[destination] ||
                bones[destination].name!=bones[source].name)return false;
            seen[destination]=true;bones[destination].parent=source;
        }
        return false;
    }
}
