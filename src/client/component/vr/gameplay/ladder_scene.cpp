#include <std_include.hpp>
#include "ladder_scene.hpp"
#include "native_carry.hpp"
#include "hands/pose_math.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
#include <utils/native_memory.hpp>
#include <unordered_map>

namespace vr::gameplay::ladders::scene
{
    namespace
    {
        template<class T>bool read(const void* p,T& value)
        {return p && utils::native_memory::read_bytes(&value,p,sizeof(value));}
        template<class T>bool at(const void* p,size_t offset,T& value)
        {return p && read(static_cast<const std::byte*>(p)+offset,value);}
        struct mesh
        {
            std::string name;std::vector<rung> edges;unsigned surface{},triangle{};
            std::vector<std::array<float,8>> vertices;std::vector<unsigned short> indices;
            std::vector<rigid_part> rigid;edge_keys seen;
            int bone{-1};anchor bind{};
            bool done{},failed{};
        };
        struct instance
        {
            std::uint32_t id{};game::XModel* model{};placement transform{};
            int entity{-1};unsigned part{},static_index{};std::uint64_t generation{},lease{};
        };
        std::unordered_map<game::XModel*,mesh> assets;
        std::vector<instance> instances;
        game::GfxWorld* world{};unsigned static_cursor{},entity_cursor{};
        std::uint64_t epoch{1},next_lease{},rejected{},queries{};
        constexpr unsigned max_instances=1024,max_edges=8192;
        bool profile(game::XModel* model)
        {
            if(const auto it=assets.find(model);it!=assets.end())return !it->second.name.empty();
            if(assets.size()>=4096)return false;
            const char* name{};std::array<char,192> text{};
            if(!at(model,0,name) || !name || !utils::native_memory::read_bytes(text.data(),name,text.size()-1))return false;
            const std::string_view key(text.data());
            // Asset families, not map names or captured entity numbers. Native
            // surface admission below rejects decorative/disabled instances.
            mesh value;game::XModel header{};
            if(supported_model(key) && read(model,header) && header.numBones && header.numRootBones==1)
            {
                if(header.numBones==1)value.name=key;
                else if(key.find("vehicle_mack_truck_short")!=key.npos)
                    for(unsigned b=1;b<header.numBones;++b)
                    {
                        game::scr_string_t id{};std::array<char,32> tag{};game::DObjAnimMat bind{};
                        if(!header.boneNames || !read(header.boneNames+b,id))break;
                        const auto* bone_name=game::SL_ConvertToString(id);
                        if(!bone_name || !utils::native_memory::read_bytes(tag.data(),bone_name,tag.size()-1) || std::string_view(tag.data())!="tag_body")continue;
                        if(!header.baseMat || !read(header.baseMat+b,bind))break;
                        value.bind={{bind.trans[0],bind.trans[1],bind.trans[2]},{bind.quat[0],bind.quat[1],bind.quat[2],bind.quat[3]}};
                        if(free_climb::finite(value.bind.position) && free_climb::rotation_valid(value.bind.rotation))
                        {value.name=key;value.bone=int(b);}
                        break;
                    }
            }
            const bool supported=!value.name.empty();assets.emplace(model,std::move(value));return supported;
        }
        void extract(game::XModel* model,mesh& cache,unsigned& budget)
        {
            game::XModel header{};
            if(!read(model,header) || !header.numBones || header.numRootBones!=1 ||
                (header.numBones!=1 && (cache.bone<0 || cache.bone>=header.numBones)) || header.lodInfo[0].numsurfs>128)
            {cache.failed=true;return;}
            while(budget && !cache.done && !cache.failed)
            {
                if(cache.surface>=header.lodInfo[0].numsurfs)
                {
                    auto key=[](const rung& e){return std::array{e.a[0],e.a[1],e.a[2],e.b[0],e.b[1],e.b[2]};};
                    std::sort(cache.edges.begin(),cache.edges.end(),[&](const auto& a,const auto& b){return key(a)<key(b);});
                    if(cache.name.find("ladder")!=cache.name.npos)cache.edges=repeated_rungs(cache.edges);
                    cache.seen.clear();cache.done=true;break;
                }
                game::XSurface s{};
                if(!read(header.lodInfo[0].surfs+cache.surface,s) || !s.vertCount || !s.triCount)
                {cache.failed=true;break;}
                // Weighted skinning is not a rigid body contact. Other parts
                // may be skinned; they do not invalidate the admitted body.
                if(s.flags&4){--budget;++cache.surface;cache.triangle=0;continue;}
                if(cache.vertices.empty())
                {
                    cache.rigid.clear();
                    for(unsigned i=0;i<s.rigidVertListCount;++i)
                    {
                        game::XRigidVertList part{};
                        if(!s.rigidVertLists || !read(s.rigidVertLists+i,part)){cache.failed=true;break;}
                        cache.rigid.push_back({part.boneOffset,part.vertCount,part.triOffset,part.triCount});
                    }
                    if(cache.failed || !valid_rigid_parts(cache.rigid,header.numBones,s.vertCount,s.triCount))
                    {cache.failed=true;break;}
                    const void* vp{};const void* ip{};
                    std::memcpy(&vp,reinterpret_cast<const std::byte*>(&s)+24,8);
                    std::memcpy(&ip,reinterpret_cast<const std::byte*>(&s)+32,8);
                    cache.vertices.resize(s.vertCount);cache.indices.resize(size_t(s.triCount)*3);
                    if(!utils::native_memory::read_bytes(cache.vertices.data(),vp,cache.vertices.size()*32) ||
                        !utils::native_memory::read_bytes(cache.indices.data(),ip,cache.indices.size()*2))
                    {cache.failed=true;break;}
                }
                for(;budget && cache.triangle<s.triCount;++cache.triangle,--budget)
                {
                    if(cache.bone>=0 && !triangle_on_bone(cache.rigid,cache.triangle,unsigned(cache.bone)))continue;
                    const auto* face=cache.indices.data()+cache.triangle*3;
                    for(unsigned side=0;side<3;++side)
                    {
                        if(face[side]>=cache.vertices.size() || face[(side+1)%3]>=cache.vertices.size()){cache.failed=true;break;}
                        const auto& av=cache.vertices[face[side]];const auto& bv=cache.vertices[face[(side+1)%3]];
                        vec a{av[0],av[1],av[2]},b{bv[0],bv[1],bv[2]};
                        if(!horizontal_edge(a,b))continue;
                        if(!append_edge(cache.edges,cache.seen,a,b,max_edges)){cache.failed=true;break;}
                    }
                    if(cache.failed)break;
                }
                if(cache.triangle==s.triCount){++cache.surface;cache.triangle=0;cache.vertices.clear();cache.indices.clear();}
            }
            if(cache.failed){cache.edges.clear();cache.seen.clear();cache.rigid.clear();cache.vertices.clear();cache.indices.clear();++rejected;}
        }
        bool refresh_instance(instance& item)
        {
            if(item.entity<0)
            {
                if(assets.at(item.model).bone>=0)return false;
                game::GfxStaticModelDrawInst value{};
                if(!world || item.static_index>=world->dpvs.smodelCount || !read(world->dpvs.smodelDrawInsts+item.static_index,value) || value.model!=item.model)return false;
                std::memcpy(item.transform.origin.data(),value.placement.origin,12);std::memcpy(item.transform.axis.data(),value.placement.axis,36);item.transform.size=value.placement.scale;
            }
            else
            {
                if(weapons::native_carry::entity_key(item.entity).generation!=item.generation)return false;
                short idx{};if(!read(reinterpret_cast<const short*>(0x14b113080)+item.entity,idx) || idx<=0 || idx>=4096)return false;
                const auto* obj=reinterpret_cast<const std::byte*>(0x14ae2dff0)+idx*0x240;
                unsigned char count{};game::XModel** models{};game::XModel* model{};
                if(!at(obj,15,count) || count!=1 || item.part>=count || !at(obj,216,models) || !read(models+item.part,model) || model!=item.model)return false;
                const auto* ent=&game::g_entities[item.entity];vec angles{};
                if(!at(ent,0xf4,item.transform.origin) || !at(ent,0x100,angles))return false;
                constexpr float rad=.017453292519943295f;const float p=angles[0]*rad,y=angles[1]*rad,r=angles[2]*rad;
                const float sp=std::sin(p),cp=std::cos(p),sy=std::sin(y),cy=std::cos(y),sr=std::sin(r),cr=std::cos(r);
                item.transform.axis={vec{cp*cy,cp*sy,-sp},vec{sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp},vec{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp}};
                item.transform.size=1;
                const auto& mesh=assets.at(item.model);
                if(mesh.bone>=0)
                {
                    // The sampled Mack rear ladder is rigid to tag_body. The
                    // same current-tag * inverse-bind composition is used by
                    // scripted-use geometry; never freeze a rendered bone pose.
                    const scripting::entity actor{game::scr_entref_t{static_cast<unsigned short>(item.entity),0}};
                    const auto tag_origin=actor.call("gettagorigin",{"tag_body"}).as<scripting::vector>();
                    const auto a=actor.call("gettagangles",{"tag_body"}).as<scripting::vector>();
                    if(!free_climb::finite({tag_origin[0],tag_origin[1],tag_origin[2]}) || !free_climb::finite({a[0],a[1],a[2]}))return false;
                    const float euler[]{a[0],a[1],a[2]};float axis[3][3]{};game::AnglesToAxis(euler,axis);
                    std::array<vec,3> rotation;std::memcpy(rotation.data(),axis,sizeof(axis));
                    const auto pose=pose_math::rigid_delta(anchor{{tag_origin[0],tag_origin[1],tag_origin[2]},from_axis(rotation)},mesh.bind);
                    item.transform.origin=pose.position;
                    item.transform.axis={rotate(pose.rotation,{1,0,0}),rotate(pose.rotation,{0,1,0}),rotate(pose.rotation,{0,0,1})};
                }
            }
            return free_climb::finite(item.transform.origin) && std::isfinite(item.transform.size) && item.transform.size>.01f && item.transform.size<100;
        }
        instance* lookup(hand_interaction::object_identity id)
        {
            for(auto& i:instances)if(i.id==id.value && i.lease==id.generation)return refresh_instance(i)?&i:nullptr;
            return nullptr;
        }
        bool native_face(vec point,vec outward,float units,vec& normal)
        {
            if(!free_climb::finite(point) || !free_climb::finite(outward) || length(outward)<.9f)return false;
            const auto from=add(point,scale(outward,.22f*units)),to=sub(point,scale(outward,.28f*units));
            game::Bounds bounds{};game::trace_t hit{};
            // PLAYERCLIP includes the authored ladder brushes. A separate solid
            // visibility query prevents admission through an intervening wall.
            game::G_TraceCapsule(&hit,from.data(),to.data(),&bounds,0,0x10000);
            if(hit.startsolid || hit.allsolid || !std::isfinite(hit.fraction) || hit.fraction>=1 || !(hit.surfaceFlags&game::SURF_FLAG_LADDER))return false;
            normal={hit.normal[0],hit.normal[1],hit.normal[2]};return std::abs(normal[2])<.35f && dot(normal,outward)>.5f;
        }
        bool visible(vec from,vec to,float clearance)
        {
            game::Bounds point{};game::trace_t hit{};game::G_TraceCapsule(&hit,from.data(),to.data(),&point,0,0x280e831);
            return !hit.startsolid && !hit.allsolid && std::isfinite(hit.fraction) && hit.fraction>=0 && hit.fraction<=1 &&
                length(sub(to,from))*(1-hit.fraction)<clearance;
        }
    }
    void reset()noexcept{assets.clear();instances.clear();world=nullptr;static_cursor=entity_cursor=0;++epoch;}
    void prepare(vec player,float units)
    {
        if(!game::CL_IsCgameInitialized() || !std::isfinite(units) || units<=0)return;
        auto* current=*game::gfx_map;if(current!=world){reset();world=current;}
        if(!world || world->dpvs.smodelCount>200000)return;
        const auto count=world->dpvs.smodelCount;
        for(unsigned end=std::min(count,static_cursor+512);static_cursor<end;++static_cursor)
        {
            game::GfxStaticModelDrawInst v{};
            if(instances.size()>=max_instances)break;
            if(!read(world->dpvs.smodelDrawInsts+static_cursor,v) || !profile(v.model))continue;
            instance item;item.id=static_cursor+1;item.model=v.model;item.static_index=static_cursor;
            if(refresh_instance(item)){item.lease=++next_lease;instances.push_back(item);}
        }
        // Script objects can spawn, move, switch destruction models or vanish.
        // Revisit a bounded slice rather than scanning all entities every frame.
        for(unsigned n=0;n<128;++n)
        {
            const int ent=int(++entity_cursor%3998)+1;const auto id=1000000u+unsigned(ent);
            auto it=std::find_if(instances.begin(),instances.end(),[&](const instance& i){return i.id==id;});
            if(it!=instances.end()){if(refresh_instance(*it))continue;instances.erase(it);}
            short idx{};if(!read(reinterpret_cast<const short*>(0x14b113080)+ent,idx) || idx<=0 || idx>=4096)continue;
            const auto* obj=reinterpret_cast<const std::byte*>(0x14ae2dff0)+idx*0x240;
            unsigned char nmodels{};game::XModel** models{};game::XModel* model{};
            if(!at(obj,15,nmodels) || nmodels!=1 || !at(obj,216,models) || !read(models,model) || !profile(model) || instances.size()>=max_instances)continue;
            instance item;item.id=id;item.model=model;item.entity=ent;item.generation=weapons::native_carry::entity_key(ent).generation;
            if(refresh_instance(item)){item.lease=++next_lease;instances.push_back(item);}
        }
        unsigned budget=4096;
        for(auto& i:instances)
        {
            if(!budget)break;
            auto& cache=assets.at(i.model);if(cache.done || cache.failed || !refresh_instance(i))continue;
            game::Bounds b{};if(!at(i.model,584,b))continue;
            const auto p=i.transform.local(player);float gap{};
            for(unsigned j=0;j<3;++j)gap+=std::pow(std::max(0.f,std::abs(p[j]-b.midPoint[j])-b.halfSize[j]),2.f);
            if(std::sqrt(gap)*i.transform.size>4*units)continue;
            extract(i.model,cache,budget);
        }
    }
    contact nearest(vec wrist,vec head,float units)
    {
        ++queries;struct option{instance* owner;unsigned edge;vec point,tangent;float distance;};std::vector<option> options;
        for(auto& i:instances)
        {
            const auto& mesh=assets.at(i.model);if(!mesh.done || mesh.failed || !refresh_instance(i))continue;
            for(unsigned e=0;e<mesh.edges.size();++e)
            {
                const auto& local=mesh.edges[e];rung edge{i.transform.world(local.a),i.transform.world(local.b)};
                const auto direction=unit(sub(edge.b,edge.a));if(std::abs(direction[2])>.18f)continue;
                const auto point=closest(edge,wrist);const float distance=length(sub(wrist,point));
                if(distance>.10f*units || length(sub(point,head))>1.4f*units)continue;
                options.push_back({&i,e,point,direction,distance});
            }
        }
        std::sort(options.begin(),options.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
        unsigned budget=24;
        for(const auto& o:options)
        {
            if(!budget--)break;
            auto outward=unit(cross(o.tangent,{0,0,1}));if(dot(outward,sub(head,o.point))<0)outward=scale(outward,-1);
            const auto& edge=assets.at(o.owner->model).edges[o.edge];
            const float clearance=std::max(.035f*units,edge.radius*o.owner->transform.size+.005f*units);
            // Centres lie inside the real rung, not on its first visible facet.
            // A wrist already touching that bar need not trace from inside its
            // own surface; the head visibility and native face still admit it.
            vec normal{};if(!native_face(o.point,outward,units,normal) || !visible(head,o.point,clearance) ||
                (o.distance>clearance && !visible(wrist,o.point,clearance)))continue;
            return {{o.owner->id,o.owner->lease},o.edge,o.point,normal,o.owner->transform.local(o.point),o.tangent,o.distance};
        }
        return {};
    }
    bool refresh(contact& c,float units)
    {
        auto* item=lookup(c.object);if(!item)return false;
        const auto& mesh=assets.at(item->model);if(c.edge>=mesh.edges.size())return false;
        const auto& e=mesh.edges[c.edge];c.point=item->transform.world(c.local);c.tangent=unit(sub(item->transform.world(e.b),item->transform.world(e.a)));
        auto normal=unit(cross(c.tangent,{0,0,1}));if(dot(normal,c.normal)<0)normal=scale(normal,-1);
        return native_face(c.point,normal,units,c.normal);
    }
    float top(const contact& c,float units)
    {
        std::vector<vec> points;
        for(auto& i:instances)
        {
            const auto& mesh=assets.at(i.model);if(!mesh.done || mesh.failed || !refresh_instance(i))continue;
            for(const auto& e:mesh.edges)
            {
                const auto a=i.transform.world(e.a),b=i.transform.world(e.b),p=scale(add(a,b),.5f);
                if(std::abs(unit(sub(b,a))[2])<.18f && same_column(p,c.point,c.normal,units))points.push_back(p);
            }
        }
        std::sort(points.begin(),points.end(),[](vec a,vec b){return a[2]>b[2];});unsigned budget=128;float last=INFINITY;
        for(auto p:points)
        {
            if(!budget--)break;if(std::abs(last-p[2])<.25f)continue;last=p[2];vec n{};
            if(native_face(p,c.normal,units,n))return p[2];
        }
        return c.point[2];
    }
    std::string status()
    {unsigned built{};for(const auto& [_,m]:assets)built+=m.done;return std::format("instances={} meshes={} rejected={} queries={} scan={} epoch={}",instances.size(),built,rejected,queries,static_cursor,epoch);}
}
