#include <std_include.hpp>
#include "cliffhanger_model.hpp"
#include "component/scene_models.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"

namespace vr::gameplay::equipment::special::cliffhanger
{
    namespace
    {
        using namespace hands;using namespace hands::pose_math;
        anchor bind(const game::DObjAnimMat& b){return {{b.trans[0],b.trans[1],b.trans[2]},normalize({b.quat[0],b.quat[1],b.quat[2],b.quat[3]})};}
        vec unpack(unsigned p){return {float(p&1023)/511.5f-1,float((p>>10)&1023)/511.5f-1,float((p>>20)&1023)/511.5f-1};}
        unsigned pack(vec v,unsigned original)
        {v=unit(v);unsigned result=original&0xc0000000u;for(unsigned i=0;i<3;++i)result|=unsigned(std::lround((std::clamp(v[i],-1.f,1.f)+1)*511.5f))<<(10*i);return result;}
        quat blend(quat a,quat b,float f)
        {float d{};for(unsigned i=0;i<4;++i)d+=a[i]*b[i];if(d<0)for(auto& x:b)x=-x;for(unsigned i=0;i<4;++i)a[i]+=(b[i]-a[i])*f;return normalize(a);}
    }
    bool model::create(game::XModel* c4,const std::array<game::XModel*,2>& pick_models,bool include_picks)
    {
        if(!c4 || !c4->name || std::string_view(c4->name)!="h2_viewmodel_c4" || c4->numBones!=8 || c4->numLods!=1 || c4->numsurfs!=2 ||
            !c4->baseMat || !c4->boneNames || !c4->lodInfo[0].surfs || !c4->materialHandles)return false;
        constexpr std::array names{"j_gun","tag_clip","tag_clip2","j_bar","j_button","j_handle_on","j_rubber_base","j_rubber_tip"};
        for(unsigned i=0;i<names.size();++i){const auto* n=game::SL_ConvertToString(c4->boneNames[i]);if(!n || std::string_view(n)!=names[i])return false;}
        source=c4;skin=*c4;surface=c4->lodInfo[0].surfs[1];material=c4->materialHandles[1];
        if(!(surface.flags&4) || surface.vertCount!=1454 || surface.triCount!=1682 || !surface.blendVerts)return false;
        skin.numsurfs=1;skin.materialHandles=&material;skin.lodInfo[0].surfs=&surface;skin.lodInfo[0].numsurfs=1;skin.lodInfo[0].surfIndex=0;
        std::size_t words{},vertices{};
        for(unsigned i=0;i<8;++i){if(surface.blendVertCounts[i]<0)return false;vertices+=surface.blendVertCounts[i];words+=surface.blendVertCounts[i]*(2*i+1);}
        if(vertices!=surface.vertCount || words>vertices*15)return false;
        std::vector<unsigned short> weights(words);
        if(!utils::native_memory::read_bytes(weights.data(),surface.blendVerts,words*2))return false;
        std::vector<std::array<float,8>> influences(vertices);
        for(std::size_t at=0,v=0,extra=0;extra<8;++extra)for(int n=0;n<surface.blendVertCounts[extra];++n,++v)
        {
            float remaining=1;
            for(std::size_t k=0;k<=extra;++k)if(weights[at+(k?2*k-1:0)]%64 || weights[at+(k?2*k-1:0)]/64>=8)return false;
            for(std::size_t k=1;k<=extra;++k){const float w=weights[at+2*k]/65535.f;remaining-=w;influences[v][weights[at+2*k-1]/64]+=w;}
            if(remaining<-.001f)return false;influences[v][weights[at]/64]+=remaining;at+=2*extra+1;
        }
        const auto bake=[&](scene_models::rigid_part& part,const std::array<anchor,8>& pose)
        {
            constexpr std::array<unsigned,6> selected{2,3,4,5,6,7};
            if(!part.create_skin_partition(&skin,selected))return false;
            std::array<anchor,8> transforms;for(unsigned b=0;b<8;++b)transforms[b]=compose(pose[b],inverse(bind(c4->baseMat[b])));
            return part.bake_vertices([&](unsigned,std::span<game::GfxPackedVertex> data)
            {
                if(data.size()!=influences.size())return false;
                for(unsigned i=0;i<data.size();++i)
                {
                    auto& vertex=data[i];vec position{},normal{},tangent{};
                    for(unsigned b=0;b<8;++b)if(const auto w=influences[i][b];w>0)
                    {
                        position=add(position,scale(compose(transforms[b],{{vertex.xyz[0],vertex.xyz[1],vertex.xyz[2]},{0,0,0,1}}).position,w));
                        normal=add(normal,scale(rotate(transforms[b].rotation,unpack(vertex.normal.packed)),w));
                        tangent=add(tangent,scale(rotate(transforms[b].rotation,unpack(vertex.tangent.packed)),w));
                    }
                    std::copy(position.begin(),position.end(),vertex.xyz);vertex.normal.packed=pack(normal,vertex.normal.packed);vertex.tangent.packed=pack(tangent,vertex.tangent.packed);
                }
                return true;
            });
        };
        if(!bake(idle,authored::detonator_idle))return false;
        const auto& bounds=idle.model()->bounds;center={bounds.midPoint[0],bounds.midPoint[1],bounds.midPoint[2]};
        for(unsigned i=0;i<steps;++i)
        {
            const float frame=float(i)*.25f;const unsigned a=std::min(unsigned(frame),25u),b=std::min(a+1,25u);const float t=frame-a;
            std::array<anchor,8> pose;
            for(unsigned j=0;j<8;++j){const auto& x=authored::detonator_fire[a][j];const auto& y=authored::detonator_fire[b][j];pose[j]={add(x.position,scale(sub(y.position,x.position),t)),blend(x.rotation,y.rotation,t)};}
            if(!bake(fire[i],pose))return false;
        }
        constexpr std::array<std::string_view,2> pick_names{"viewmodel_ice_picker_03","viewmodel_ice_picker"};
        if(include_picks)for(unsigned i=0;i<2;++i)if(!pick_models[i] || !pick_models[i]->name || std::string_view(pick_models[i]->name)!=pick_names[i] ||
            pick_models[i]->numBones!=2 || !picks[i].create(pick_models[i],0))return false;
        if(include_picks)for(unsigned h=0;h<2;++h)
        {
            const auto* pick=pick_models[h];if(!pick->boneNames || !pick->baseMat)return false;
            int tip=-1;for(int b=0;b<2;++b){const auto* name=game::SL_ConvertToString(pick->boneNames[b]);if(name && std::string_view(name)=="tag_ice_picker_fx")tip=b;}
            if(tip<0)return false;
            pick_tips[h]=compose(inverse(bind(pick->baseMat[0])),bind(pick->baseMat[tip])).position;
        }
        std::array<scene_models::runtime_model,steps+3> registrations{};registrations[0]={idle.model(),source};
        for(unsigned i=0;i<steps;++i)registrations[i+1]={fire[i].model(),source};
        if(include_picks)for(unsigned i=0;i<2;++i)registrations[steps+1+i]={picks[i].model(),pick_models[i]};
        return scene_models::register_runtime_models(std::span{registrations}.first(steps+1+(include_picks?2:0)));
    }
}
