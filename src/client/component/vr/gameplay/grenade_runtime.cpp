#include <std_include.hpp>
#include "grenade_runtime.hpp"
#include "grenade_profile.hpp"
#include "grenade_contact.hpp"
#include "part_hand_constraint.hpp"
#include "knife_profile.hpp"
#include "native_grenade.hpp"
#include "chest_equipment.hpp"
#include "native_scripted_control.hpp"
#include "weapon_feedback.hpp"
#include "native_weapon_sound.hpp"
#include "hands/attachment_pose.hpp"
#include "hand_interaction/runtime.hpp"
#include "../engine_stereo_view.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::grenades
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		namespace hi=hand_interaction;
		using clock=controller_input::clock;
		struct asset
		{
			std::array<std::unique_ptr<scene_models::rigid_part>,8> pieces;
			std::array<anchor,8> local{};
			unsigned count{},pin_mask{},lever_mask{};float radius{};vec center{},lever_pivot{},pin{},pin_low{1e7f,1e7f,1e7f},pin_high{-1e7f,-1e7f,-1e7f};
		};
		struct held
		{
			state value;anchor root{};vec pull_start{},pull_delta{},velocity{};
			int grasp_started{},pin_started{};clock::time_point grasp_at{},pin_at{};quat pull_rotation{0,0,0,1};
			release_motion motion;std::uint64_t button_generation{},button_presses{};
		};
		struct pin_debris {bool active{};kind type{};anchor root{};vec velocity{};int started{};std::uint64_t reference{};unsigned mask{};};
		struct sound_event {std::uint32_t weapon{};bool release{};vec origin{};std::uint64_t reference{},epoch{},at{};};
		struct snapshot
		{
			std::array<held,2> items{};std::array<std::shared_ptr<asset>,kind_count> assets{};
			std::array<pin_debris,4> debris{};
			std::array<quat,2> basis{},mirror{};bool hands_ready{};
			std::uint64_t reference{};clock::time_point at{};
		};
		std::mutex mutex;
		snapshot published,submitted;
		std::array<held,2> items;
		equipment::grab_intent draw_intent;
		std::array<pin_debris,4> debris;
		std::array<std::shared_ptr<asset>,kind_count> assets;
		std::array<std::array<unsigned short,8>,kind_count> lighting{};
		std::array<std::array<unsigned short,8>,kind_count> debris_lighting{};
		std::mutex sound_mutex;std::array<sound_event,16> sounds{};unsigned sound_count{};
		std::atomic_uint64_t sound_epoch{},sounds_played{},sounds_missing{};
		std::atomic_bool alive{true},ready{};
		game::dvar_t *enabled{},*throw_gain{},*football_gain{};
		std::atomic<float> last_raw_speed{},last_throw_speed{};
		std::atomic_uint64_t takes{},pins{},returns{},throws{},cooks{},in_hand{},failed{},handoffs{};
		std::atomic<const char*> reason{"waiting for native grenade resources"};
		const void* player{};int last_time{};
		bool running()noexcept{return alive && ready && enabled && enabled->current.enabled && weapons::carry::active();}
		bool fresh(clock::time_point at)noexcept{const auto now=clock::now();return now>=at && now-at<=150ms;}
		anchor bind(const game::DObjAnimMat& b)noexcept{return {{b.trans[0],b.trans[1],b.trans[2]},normalize({b.quat[0],b.quat[1],b.quat[2],b.quat[3]})};}
		void publish(const hi::frame* frame=nullptr)
		{
			const std::lock_guard lock(mutex);published.items=items;published.debris=debris;
			if(frame){published.reference=frame->input.reference_generation;published.at=frame->input.sampled_at;}
		}
		void queue_sound(const held& item,bool released)
		{
			const std::lock_guard lock(sound_mutex);
			if(sound_count==sounds.size()){++sounds_missing;return;}
			sounds[sound_count++]={item.value.weapon,released,item.root.position,item.value.reference,sound_epoch.load(),GetTickCount64()};
		}
		void play_sounds()
		{
			std::array<sound_event,16> batch{};unsigned count{};
			{const std::lock_guard lock(sound_mutex);count=sound_count;std::copy_n(sounds.begin(),count,batch.begin());sound_count=0;}
			const auto input=controller_input::latest();const auto now=GetTickCount64();
			const auto* paused=game::Dvar_FindVar("cl_paused");
			for(unsigned i=0;i<count;++i)
			{
				const auto& event=batch[i];
				if(!alive || event.epoch!=sound_epoch || event.reference!=input.reference_generation || !input.focused ||
					now<event.at || now-event.at>250 || !paused || paused->current.integer)continue;
				if(weapons::native_weapon_sound::play_offhand(event.weapon,event.release,event.origin))++sounds_played;else ++sounds_missing;
			}
		}
		void include_pin(asset& a,unsigned part)
		{
			const auto& bounds=a.pieces[part]->model()->bounds;
			for(unsigned corner=0;corner<8;++corner)
			{
				vec point{};for(unsigned c=0;c<3;++c)point[c]=bounds.midPoint[c]+((corner&(1u<<c))?1.f:-1.f)*bounds.halfSize[c];
				point=compose(a.local[part],{point,{0,0,0,1}}).position;
				for(unsigned c=0;c<3;++c){a.pin_low[c]=std::min(a.pin_low[c],point[c]);a.pin_high[c]=std::max(a.pin_high[c],point[c]);}
			}
			a.pin=scale(add(a.pin_low,a.pin_high),.5f);
		}
		void refresh()
		{
			if(!alive || !game::CL_IsCgameInitialized() || !scene_models::ready())return;
			for(unsigned k=0;k<kind_count;++k)
			{
				if(assets[k])continue;
				const auto& profile=authored::profiles[k];
				auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,profile.model,0).model;
				if(!source || !source->name || std::string_view(source->name)!=profile.model || source->numBones<2 || source->numBones>8 ||
					!source->boneNames || !source->baseMat || source->numLods!=1 || !source->lodInfo[0].surfs)continue;
				const auto* root_name=game::SL_ConvertToString(source->boneNames[1]);if(!root_name || std::string_view(root_name)!="tag_clip")continue;
				auto next=std::make_shared<asset>();next->count=source->numBones;next->pin_mask=profile.pin_mask;
				const auto root=inverse(bind(source->baseMat[1]));bool valid=true;unsigned geometry{};
				const auto& source_bounds=source->bounds;
				bool bounds_valid=true;
				for(unsigned c=0;c<3;++c)bounds_valid=bounds_valid && std::isfinite(source_bounds.midPoint[c]) &&
					std::abs(source_bounds.midPoint[c])<10000 && std::isfinite(source_bounds.halfSize[c]) && source_bounds.halfSize[c]>0 && source_bounds.halfSize[c]<100;
				if(!bounds_valid)continue;
				next->center=compose(root,{{source_bounds.midPoint[0],source_bounds.midPoint[1],source_bounds.midPoint[2]},{0,0,0,1}}).position;
				next->radius=std::max({source_bounds.halfSize[0],source_bounds.halfSize[1],source_bounds.halfSize[2]});
				// Pomegranate performs the same two-hand safety gesture at its crown,
				// but owns no pin/lever meshes. No other asset has to be loaded first.
				if(k==unsigned(kind::pomegranate))next->pin=compose(root,{{source_bounds.midPoint[0],source_bounds.midPoint[1]+source_bounds.halfSize[1]*.35f,source_bounds.midPoint[2]+source_bounds.halfSize[2]*.8f},{0,0,0,1}}).position;

				if(k==unsigned(kind::frag))
				{
					if(source->numsurfs!=1 || source->lodInfo[0].surfs[0].vertCount!=authored::frag_vertices || source->lodInfo[0].surfs[0].triCount!=authored::frag_triangles)continue;
					std::array<std::byte,authored::frag_triangles*6> triangles{};
					if(!utils::native_memory::read_bytes(triangles.data(),source->lodInfo[0].surfs[0].triIndices,triangles.size()))continue;
					std::uint32_t hash=2166136261u;for(auto b:triangles)hash=(hash^std::to_integer<unsigned char>(b))*16777619u;
					if(hash!=authored::frag_triangles_hash){reason="M67 lever topology changed";continue;}
				}

				std::array<scene_models::runtime_model,8> identity{};unsigned registered{},pin_count{};
				bool skinned{};for(unsigned n=0;n<source->numsurfs;++n)skinned=skinned || (source->lodInfo[0].surfs[n].flags&4u);
				if(skinned)
				{
					unsigned used{};
					for(unsigned n=0;n<source->numsurfs && valid;++n)
					{
						const auto& surf=source->lodInfo[0].surfs[n];std::size_t words{},vertices{};
						for(unsigned j=0;j<8;++j){if(surf.blendVertCounts[j]<0){valid=false;break;}vertices+=surf.blendVertCounts[j];words+=surf.blendVertCounts[j]*(2*j+1);}
						if(!valid || vertices!=surf.vertCount || words>65535*15){valid=false;break;}
						std::vector<std::uint16_t> blend(words);
						if(!utils::native_memory::read_bytes(blend.data(),surf.blendVerts,words*2)){valid=false;break;}
						for(std::size_t at=0,j=0;j<8 && valid;++j)for(int v=0;v<surf.blendVertCounts[j] && valid;++v)
						{
							for(std::size_t influence=0;influence<=j;++influence){const auto off=blend[at+(influence?2*influence-1:0)];
								if(off%64 || off/64>=source->numBones){valid=false;break;}used|=1u<<(off/64);}
							at+=2*j+1;
						}
					}
					if(!valid)continue;
					next->count=profile.pin_mask?2:1;next->pin_mask=profile.pin_mask?2:0;
					for(unsigned part=0;part<next->count && valid;++part)
					{
						std::array<unsigned,8> members{};unsigned count{};
						for(unsigned b=0;b<source->numBones;++b)if((used&(1u<<b)) && bool(profile.pin_mask&(1u<<b))==bool(part))members[count++]=b;
						if(!count){valid=false;break;}
						next->pieces[part]=std::make_unique<scene_models::rigid_part>();
						if(!next->pieces[part]->create_skin_partition(source,{members.data(),count})){reason=next->pieces[part]->status();valid=false;break;}
						next->local[part]=root;identity[registered++]={next->pieces[part]->model(),source};
						if(part){include_pin(*next,part);++pin_count;}
					}
					if(!valid || (profile.pin_mask && !pin_count) || !scene_models::register_runtime_models({identity.data(),registered}))continue;
					assets[k]=std::move(next);
					{const std::lock_guard lock(mutex);published.assets=assets;}reason="native skinned grenade partitions ready";continue;
				}
				for(unsigned b=0;b<next->count;++b)
				{
					// Subset vertices remain in common MODEL bind space, including
					// rigid groups. Applying the bone bind again displaces the ring.
					next->local[b]=root;
					bool has_mesh{};
					for(unsigned n=0;n<source->numsurfs;++n)
					{
						const auto& surf=source->lodInfo[0].surfs[n];
						if(surf.rigidVertListCount>32 || (surf.rigidVertListCount && !surf.rigidVertLists)){valid=false;break;}
						for(unsigned g=0;g<surf.rigidVertListCount;++g)
							if(surf.rigidVertLists[g].boneOffset==b*64 && surf.rigidVertLists[g].triCount)has_mesh=true;
					}
					if(!valid)break;
					if(!has_mesh)continue;
					next->pieces[b]=std::make_unique<scene_models::rigid_part>();
					const bool created=k==unsigned(kind::frag) && b==1 ? next->pieces[b]->create_face_partition(source,b,authored::frag_lever_faces,false) : next->pieces[b]->create(source,b);
					if(!created){reason=next->pieces[b]->status();valid=false;break;}
					identity[registered++]={next->pieces[b]->model(),source};geometry|=1u<<b;
					if(profile.pin_mask&(1u<<b)){include_pin(*next,b);++pin_count;}
				}
				if(!valid || !registered || (profile.pin_mask && !pin_count) || !(geometry&~profile.pin_mask))continue;
				if(k==unsigned(kind::frag))
				{
					next->pieces[0]=std::make_unique<scene_models::rigid_part>();
					if(!next->pieces[0]->create_face_partition(source,1,authored::frag_lever_faces,true)){reason=next->pieces[0]->status();continue;}
					next->local[0]=root;next->lever_mask=1;identity[registered++]={next->pieces[0]->model(),source};
					const auto& bounds=next->pieces[0]->model()->bounds;
					next->lever_pivot=compose(root,{{bounds.midPoint[0]+bounds.halfSize[0],bounds.midPoint[1],bounds.midPoint[2]+bounds.halfSize[2]},{0,0,0,1}}).position;
				}

				if(!scene_models::register_runtime_models({identity.data(),registered}))continue;
				assets[k]=std::move(next);
				{const std::lock_guard lock(mutex);published.assets=assets;}
				reason="native grenade models ready";
			}
		}
		std::array<std::uint32_t,2> selected()noexcept
		{
			std::array<std::byte,0x3bc> bytes{};
			return utils::native_memory::read_bytes(bytes.data(),game::g_entities[0].client,bytes.size())?equipment::selected_chest_items(bytes):std::array<std::uint32_t,2>{};
		}
		hi::target target(unsigned slot,unsigned component=0)noexcept
		{return hi::object(hi::domain::grenade,{items[slot].value.weapon,items[slot].value.revision},component);}
		void release(held& item,int time,vec velocity)
		{
			if(item.value.stage==phase::release_pending)return;
			if(item.value.stage==phase::safe){item.value.stow();item.motion.reset();++returns;return;}
			item.value.release(time);item.velocity=velocity;item.pull_delta={};
		}
		void emit(held& item,int time)
		{
			if(item.value.stage!=phase::release_pending)return;
			if(!item.value.spent && !native::available(item.value.weapon)){item.value.stow();item.motion.reset();reason="unpaid throwable no longer in native inventory";return;}
			if(native::launch(item.value.weapon,item.root.position,item.velocity,item.value.fuse_at(time),item.value.spent))
			{queue_sound(item,true);item.value.stow();item.motion.reset();++throws;reason="native grenade released";}
			else{++failed;reason="native grenade spawn rejected; committed grenade retained";}
		}
		void retire()
		{
			// Drained zone boundary: native inventory is being replaced as well.
			draw_intent.reset();items={};debris={};player=nullptr;last_time=0;
			{const std::lock_guard lock(sound_mutex);++sound_epoch;sound_count=0;}
			{const std::lock_guard lock(mutex);published={};submitted={};}
			hands::attachments::clear_after_drain();assets={};
		}
		void submit()
		{
			snapshot s;{const std::lock_guard lock(mutex);s=published;submitted=s;}
			if(!running() || !fresh(s.at))return;
			for(unsigned slot=0;slot<2;++slot)
			{
				const auto& item=s.items[slot];if(!item.value.held() || item.value.stage==phase::release_pending)continue;
				auto a=s.assets[unsigned(item.value.type)];if(!a)continue;
				for(unsigned b=0;b<a->count;++b)
				{
					if(!a->pieces[b] || (item.value.spent && (a->pin_mask&(1u<<b))) || (item.value.stage==phase::cooking && (a->lever_mask&(1u<<b))))continue;
					auto local=a->local[b];if(a->pin_mask&(1u<<b))local.position=add(local.position,item.pull_delta);
					const auto world=compose(item.root,local);game::GfxScaledPlacement placement{};placement.scale=1;
					std::copy(world.position.begin(),world.position.end(),placement.base.origin);std::copy(world.rotation.begin(),world.rotation.end(),placement.base.quat);
					float color[4]{1,1,1,1};scene_models::submit(a->pieces[b]->model(),&placement,scene_models::no_cast_shadow,&lighting[unsigned(item.value.type)][b],color,color,color,8.f);
				}
			}
			for(const auto& pin:s.debris)
			{
				const int age=native::time()-pin.started;if(!pin.active || age<0 || age>450 || pin.reference!=s.reference)continue;
				const auto a=s.assets[unsigned(pin.type)];if(!a)continue;
				const float seconds=age*.001f;auto root=pin.root;
				root.position=add(root.position,scale(pin.velocity,seconds));root.position[2]-=400.f*seconds*seconds;
				if(pin.mask&a->lever_mask)
				{
					const auto pivot=compose(root,{a->lever_pivot,{0,0,0,1}}).position;
					root.rotation=normalize(multiply(root.rotation,{0,std::sin(seconds*9.f),0,std::cos(seconds*9.f)}));
					root.position=sub(pivot,rotate(root.rotation,a->lever_pivot));
				}
				for(unsigned b=0;b<a->count;++b)if(a->pieces[b] && (pin.mask&(1u<<b)))
				{
					const auto world=compose(root,a->local[b]);game::GfxScaledPlacement placement{};placement.scale=1;
					std::copy(world.position.begin(),world.position.end(),placement.base.origin);std::copy(world.rotation.begin(),world.rotation.end(),placement.base.quat);
					float color[4]{1,1,1,1};scene_models::submit(a->pieces[b]->model(),&placement,0,&debris_lighting[unsigned(pin.type)][b],color,color,color,8.f);
				}
			}
		}
		scene_models::placement_result prepare(const scene_models::preparation& p,const void* entry,game::GfxPlacement& current,game::GfxPlacement& previous)noexcept
		{
			using result=scene_models::placement_result;
			std::uintptr_t handle{};if(!utils::native_memory::read_bytes(&handle,static_cast<const std::byte*>(entry)+0x68,8))return result::unchanged;
			// Foreign scene entries must not acquire our publication lock or copy
			// hand state. These stable handles also outlive queued render work.
			const auto begin=reinterpret_cast<std::uintptr_t>(lighting.data());
			if(handle<begin || handle>=begin+sizeof(lighting) || (handle-begin)%sizeof(unsigned short))return result::unchanged;
			snapshot s,live;{const std::lock_guard lock(mutex);s=submitted;live=published;}
			for(unsigned slot=0;slot<2;++slot)
			{
				const auto& item=s.items[slot];if(!item.value.held())continue;auto a=s.assets[unsigned(item.value.type)];if(!a)continue;
				for(unsigned b=0;b<a->count;++b)
				{
					if(handle!=reinterpret_cast<std::uintptr_t>(&lighting[unsigned(item.value.type)][b]))continue;
					const auto& current_item=live.items[slot];
					if(!fresh(live.at) || item.value.revision!=current_item.value.revision || item.value.stage!=current_item.value.stage ||
						item.value.reference!=live.reference || item.value.holder!=current_item.value.holder)return result::omit;
					std::array<float,12> camera{};vec origin{};attachments::solved pose;
					if(!utils::native_memory::read_bytes(camera.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_view_origin_offset,sizeof(camera)) ||
						!attachments::for_record(p.record,camera,pose) || pose.reference!=item.value.reference ||
						!utils::native_memory::read_bytes(origin.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_current_model_placement_origin_offset,sizeof(origin)))return result::omit;
					const auto h=unsigned(item.value.holder);if(h>1)return result::omit;
					const auto root=compose({add(pose.wrists[h].position,origin),pose.wrists[h].rotation},authored::attachment(item.value.type,pose.mirror_basis[h],h==0));
					auto local=a->local[b];
					if(a->pin_mask&(1u<<b))
					{
						auto travel=item.pull_delta;
						if(vr::valid_hand(item.value.puller))
						{
							const auto off=unsigned(item.value.puller);auto point=authored::left_pinch[unsigned(item.value.type)];
							if(off==1)point=hands::pose_mirror::local_point(point,pose.mirror_basis[off]);
							const auto contact=compose({add(pose.wrists[off].position,origin),pose.wrists[off].rotation},{point,{0,0,0,1}}).position;
							travel=pin_slide(a->pin,compose(inverse(root),{contact,{0,0,0,1}}).position,pose.units);
						}
						local.position=add(local.position,travel);
					}
					const auto world=compose(root,local);std::copy(world.position.begin(),world.position.end(),current.origin);std::copy(world.rotation.begin(),world.rotation.end(),current.quat);
					previous=current;return result::replace;
				}
			}
			return result::unchanged;
		}
	}
	bool occupies_slot(unsigned slot)noexcept
	{const std::lock_guard lock(mutex);return slot<2 && published.items[slot].value.held();}
	void report_interactions()noexcept
	{
		for(unsigned i=0;i<2;++i)
		{
			const auto& s=items[i].value;if(!s.held())continue;
			if(vr::valid_hand(s.holder))hi::observed(s.holder,{target(i),hi::role::tactical,hi::button::grip,hi::recipe::single,hi::capability::action});
			if(vr::valid_hand(s.puller))hi::observed(s.puller,{target(i,1),hi::role::part,hi::button::trigger,hi::recipe::single,hi::capability::action});
		}
	}
	void collect_interactions(const hi::frame& f)noexcept
	{
		if(!running()){draw_intent.reset();return;}
		snapshot pub;{const std::lock_guard lock(mutex);pub=published;}
		if(!pub.hands_ready){draw_intent.reset();return;}
		const auto tokens=selected();const auto chest=equipment::locate_chest(f.body);if(!chest.valid){draw_intent.reset();return;}
		unsigned pressed{},released{},available{};
		for(unsigned h=0;h<2;++h){const auto edge=hi::input(vr::hand(h),hi::button::grip);if(edge.press)pressed|=1u<<h;if(edge.release)released|=1u<<h;if(hi::free(vr::hand(h)))available|=1u<<h;}
		const auto pending=draw_intent.consume(f.input,available&f.valid_hands,pressed,released);
		for(unsigned h=0;h<2;++h)
		{
			const auto actor=vr::hand(h);if(!hi::free(actor) || !(f.valid_hands&(1u<<h)))continue;
			const auto grip=hi::input(actor,hi::button::grip),trigger=hi::input(actor,hi::button::trigger);
			if(!(pending&(1u<<h)) && !(grip.press && grip.down) && !(trigger.press && trigger.down))continue;
			for(unsigned i=0;i<2;++i)
			{
				const auto& item=items[i];const auto& state=item.value;native::descriptor desc;
				if(!state.held())
				{
					if(!sequences::chest_equipment_visible()){draw_intent.reset();continue;}
					if(!(pending&(1u<<h)) || !native::describe(tokens[i],desc) || !pub.assets[unsigned(desc.type)] || !native::available(tokens[i]))continue;
					if(std::any_of(items.begin(),items.end(),[&](const held& other){return other.value.held() && other.value.type==desc.type;}))continue;
					const auto palm=equipment::knife_profile::palm_contact(f.wrists[h],pub.basis[h],pub.mirror[h],h==1);
					const float distance=equipment::chest_grab_distance(chest,equipment::item_slots[i],palm);
					if(distance<=1)hi::offer({actor,{hi::object(hi::domain::grenade,{tokens[i],state.revision+1}),hi::role::tactical,hi::button::grip,hi::recipe::single,hi::capability::action},grip.event,20,distance,1,true,true});
					continue;
				}
				if(unsigned(state.holder)==h || state.stage==phase::release_pending || vr::valid_hand(state.puller))continue;
				auto a=pub.assets[unsigned(state.type)];if(!a)continue;
				const unsigned holder=unsigned(state.holder);if(holder>1 || !(f.valid_hands&(1u<<holder)))continue;
				auto wrist=f.wrists[holder];wrist.rotation=normalize(multiply(wrist.rotation,pub.basis[holder]));
				const auto root=compose(wrist,authored::attachment(state.type,pub.mirror[holder],holder==0));
				const auto contact=pinch_point(state.type,f.wrists[h],pub.basis[h],pub.mirror[h],h);
				if(behaviors[unsigned(state.type)].pin_gesture && state.stage==phase::safe && trigger.press && trigger.down)
				{
					const auto pin=compose(root,{a->pin,{0,0,0,1}}).position;
					const float distance=segment_distance(pin,f.wrists[h].position,contact)/(f.body.units_per_meter*pin_acquire_meters);
					if(distance<=1)hi::offer({actor,{target(i,1),hi::role::part,hi::button::trigger,hi::recipe::single,hi::capability::action},trigger.event,10,distance,1,true,true});
				}
				if(grip.press && grip.down)
				{
					const float distance=segment_distance(compose(root,{a->center,{0,0,0,1}}).position,f.wrists[h].position,contact)/std::max(f.body.units_per_meter*.09f,a->radius+f.body.units_per_meter*.025f);
					if(distance<=1)hi::offer({actor,{target(i,2),hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action},grip.event,12,distance,1,true,true});
				}
			}
		}
	}
	void update_interactions()noexcept
	{
		const auto* frame=hi::simulation();if(!frame || !running())return;
		const auto& f=*frame;const int now=native::time();const auto tokens=selected();
		snapshot pub;{const std::lock_guard lock(mutex);pub=published;}
		for(unsigned i=0;i<2;++i)
		{
			auto& item=items[i];auto& s=item.value;
			if(!s.held())for(unsigned h=0;h<2;++h)
			{
				native::descriptor d;
				if(!native::describe(tokens[i],d) || !pub.assets[unsigned(d.type)] || !native::available(tokens[i]))continue;
				if(std::any_of(items.begin(),items.end(),[&](const held& other){return other.value.held() && other.value.type==d.type;}))continue;
				if(hi::granted(vr::hand(h),hi::domain::grenade,{tokens[i],s.revision+1},hi::button::grip,hi::role::tactical) &&
					s.take(d.type,d.weapon,vr::hand(h),f.input.reference_generation,d.fuse))
				{item.motion.reset();item.grasp_started=now;item.grasp_at=f.input.sampled_at;item.pull_delta={};item.button_generation=f.input.secondary[h].generation;item.button_presses=f.input.secondary[h].presses;++takes;weapons::feedback::carry_confirmation(vr::hand(h),f.input);break;}
			}
			if(!s.held())continue;
			if(s.stage==phase::release_pending){emit(item,now);continue;}
			// Commit handoff before processing the old holder's release edge.
			// A live pin grasp blocks both candidate generation and this state gate.
			const auto next=vr::hand(1-unsigned(s.holder));const auto handoff_target=target(i,2);
			if(hi::granted(next,hi::domain::grenade,{s.weapon,s.revision},hi::button::grip,hi::role::control) && s.handoff(next))
			{
				hi::completed(next,handoff_target);item.motion.reset();item.pull_delta={};item.grasp_started=now;item.grasp_at=f.input.sampled_at;
				item.button_generation=f.input.secondary[unsigned(next)].generation;item.button_presses=f.input.secondary[unsigned(next)].presses;
				++handoffs;weapons::feedback::carry_confirmation(next,f.input);
			}
			const unsigned h=unsigned(s.holder);if(h>1)continue;
			if((f.valid_hands&(1u<<h)) && s.reference==f.input.reference_generation)
			{
				auto wrist=f.wrists[h];wrist.rotation=normalize(multiply(wrist.rotation,pub.basis[h]));
				item.root=compose(wrist,authored::attachment(s.type,pub.mirror[h],h==0));
				item.motion.sample(item.root.position,f.input.sampled_at,f.body.units_per_meter);
			}
			if(s.stage==phase::safe && (tokens[i]!=s.weapon || !native::available(s.weapon))){s.stow();continue;}
			const auto grip=hi::input(s.holder,hi::button::grip);
			if(grip.release || (f.input.squeeze[h].active && !f.input.squeeze[h].down))
			{
				const auto chest=equipment::locate_chest(f.body);
				const auto palm=equipment::knife_profile::palm_contact(f.wrists[h],pub.basis[h],pub.mirror[h],h==1);
				if((f.valid_hands&(1u<<h)) && tokens[i]==s.weapon &&
					equipment::chest_grab_distance(chest,equipment::item_slots[i],palm)<=1 && s.return_to_chest())
				{item.motion.reset();++returns;reason="football returned to chest";continue;}
				const auto raw=item.motion.velocity(f.input.sampled_at,f.body.units_per_meter);
				const auto velocity=throw_velocity(raw,f.body.units_per_meter,s.type,throw_gain?throw_gain->current.value:throw_gain_default,football_gain?football_gain->current.value:football_gain_default);
				if(s.stage!=phase::safe){last_raw_speed=length(raw)/f.body.units_per_meter;last_throw_speed=length(velocity)/f.body.units_per_meter;}
				release(item,now,velocity);emit(item,now);continue;
			}
			if(s.stage==phase::safe && behaviors[unsigned(s.type)].pin_gesture)
			{
				const unsigned other=1-h;
				if(hi::granted(vr::hand(other),hi::domain::grenade,{s.weapon,s.revision},hi::button::trigger,hi::role::part) && s.puller==vr::hand::none)
				{s.puller=vr::hand(other);item.pin_started=now;item.pin_at=f.input.sampled_at;item.pull_start=compose(inverse(item.root),f.wrists[other]).position;item.pull_rotation=normalize(multiply(conjugate(item.root.rotation),multiply(f.wrists[other].rotation,pub.basis[other])));item.pull_delta={};}
				if(vr::valid_hand(s.puller))
				{
					if(!(f.valid_hands&(1u<<other)) || (!f.input.trigger[other].active || !f.input.trigger[other].down)){s.puller=vr::hand::none;item.pull_delta={};}
					else
					{
						const auto tracked=compose(inverse(item.root),f.wrists[other]).position;
						const float offset=length(sub(tracked,item.pull_start))/f.body.units_per_meter;
						item.pull_delta=pin_slide(item.pull_start,tracked,f.body.units_per_meter);
						const float distance=item.pull_delta[1]/f.body.units_per_meter;
						if(offset>.5f){s.puller=vr::hand::none;item.pull_delta={};}
						else if(distance>=pin_stroke_meters-.0001f && s.pull_pin(native::debit(s.weapon)))
						{
							++pins;queue_sound(item,false);
							auto root=item.root;root.position=add(root.position,rotate(root.rotation,item.pull_delta));
							const auto speed=std::max(.05f,(now-item.pin_started)*.001f);
							auto velocity=rotate(root.rotation,scale(item.pull_delta,1/speed));const float length_=length(velocity),limit=f.body.units_per_meter*2;
							if(length_>limit)velocity=scale(velocity,limit/length_);
							const auto mask=pub.assets[unsigned(s.type)]->pin_mask;
							debris[i]={mask!=0,s.type,root,velocity,now,s.reference,mask};
							item.pull_delta={};weapons::feedback::carry_confirmation(vr::hand(other),f.input);reason="pin removed; release to throw";
						}
					}
				}
			}
			const auto& button=f.input.secondary[h];
			const bool pressed=button.active && button.generation==item.button_generation && button.presses>item.button_presses;
			item.button_generation=button.generation;item.button_presses=button.presses;
			if(pressed && s.cook(now))
			{
				queue_sound(item,false);++cooks;weapons::feedback::carry_confirmation(s.holder,f.input);reason="frag cooking";
				const auto a=pub.assets[unsigned(s.type)];
				if(a && a->lever_mask)debris[i+2]={true,s.type,item.root,rotate(item.root.rotation,{-1.5f*f.body.units_per_meter,0,1.8f*f.body.units_per_meter}),now,s.reference,a->lever_mask};
			}
			if(s.due(now)){release(item,now,{});++in_hand;emit(item,now);}
		}
		publish(&f);
	}
	void lifecycle(bool suspended)noexcept
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || !ready)return;
		if(suspended || !running())draw_intent.reset();
		const auto* ps=game::g_entities[0].client;const int now=native::time();
		if(!game::CL_IsCgameInitialized() || !ps || (player && (player!=ps || now<last_time)))
		{items={};debris={};player=ps;last_time=now;publish();return;}
		player=ps;last_time=now;
		const auto* paused=game::Dvar_FindVar("cl_paused");if(!paused || paused->current.integer)return;
		const auto input=controller_input::latest();
		for(auto& item:items)
		{
			auto& s=item.value;if(!s.held())continue;
			if(s.stage==phase::release_pending){emit(item,now);continue;}
			const bool lost=suspended || !running() || !input.focused || !fresh(input.sampled_at) || input.reference_generation!=s.reference ||
				!scripted_control::allowed(ps) || !input.grip[unsigned(s.holder)].valid || !input.aim[unsigned(s.holder)].valid ||
				(reinterpret_cast<const game::playerState_s*>(ps)->e_flags&0x103000) || weapons::carry::hand_has_weapon(s.holder);
			if(lost)
			{
				if(!s.spent){s.stow();item.motion.reset();continue;}
				const auto* state=reinterpret_cast<const game::playerState_s*>(ps);
				item.root.position={state->origin[0],state->origin[1],state->origin[2]+state->viewHeightCurrent-10};
				release(item,now,{});
			}
			else if(s.due(now)){release(item,now,{});++in_hand;}
			emit(item,now);
		}
		publish();
	}
	void present(const hands::interaction_rig& parts,const rig& r,const controller_input::frame& input,
		const std::array<anchor,2>& targets,const std::array<vec,2>& shoulders,const std::array<vec,3>& axes,float units,std::span<bone> solved,unsigned occupied,unsigned visible)noexcept
	{
		if(!parts.valid || r.count<=0 || r.count>256 || solved.size()<std::size_t(r.count))return;
		snapshot s;{const std::lock_guard lock(mutex);published.basis=parts.basis;published.hands_ready=true;
			for(unsigned h=0;h<2;++h)published.mirror[h]=parts.library.mirror_basis[r.arms[h].wrist];s=published;}
		if(!running() || !fresh(s.at) || s.reference!=input.reference_generation)return;
		for(const auto& item:s.items)
		{
			if(!item.value.held())continue;const auto& profile=authored::profiles[unsigned(item.value.type)];
			for(unsigned h=0;h<2;++h)
			{
				if(!(visible&(1u<<h)) || (occupied&(1u<<h)) || !input.grip[h].valid)continue;
				const bool holder=unsigned(item.value.holder)==h,puller=unsigned(item.value.puller)==h;if(!holder && !puller)continue;
				if(puller)
				{
					const auto a=s.assets[unsigned(item.value.type)];const auto rear=unsigned(item.value.holder);if(!a || rear>1)continue;
					const auto root=compose({solved[r.arms[rear].wrist].position,normalize(multiply(targets[rear].rotation,parts.basis[rear]))},authored::attachment(item.value.type,s.mirror[rear],rear==0));
					const auto travel=pin_slide(item.pull_start,compose(inverse(root),targets[h]).position,units);
					const auto rotation=normalize(multiply(root.rotation,item.pull_rotation));
					auto point=authored::left_pinch[unsigned(item.value.type)];if(h==1)point=hands::pose_mirror::local_point(point,s.mirror[h]);
					const auto contact=compose(root,{add(a->pin,travel),{0,0,0,1}}).position;
					const anchor desired{sub(contact,rotate(rotation,point)),rotation};
					(void)weapons::constrain_part_hand(r,parts.library,hands::native_hand_schema::definition,targets,shoulders,axes,int(rear),desired,solved);
				}
				else move_part(r,r.arms[h].wrist,{solved[r.arms[h].wrist].position,normalize(multiply(targets[h].rotation,parts.basis[h]))},solved);
				std::array<bone,256> before{},desired{};std::copy_n(solved.begin(),r.count,before.begin());
				hands::pose_mirror::fingers(r,parts.library,hands::native_hand_schema::definition,holder?profile.grip:profile.pinch,h,solved,holder?h==0:h==1);
				std::copy_n(solved.begin(),r.count,desired.begin());
				const float amount=std::clamp(std::chrono::duration<float>(input.sampled_at-(holder?item.grasp_at:item.pin_at)).count()/.12f,0.f,1.f);
				for(int b=r.arms[h].wrist+1;b<r.count;++b)if(descendant(b,r.arms[h].wrist,r) && r.parent[b]>=0)
				{
					const int parent=r.parent[b];
					const auto from=compose(inverse(as_anchor(before[parent])),as_anchor(before[b]));
					const auto to=compose(inverse(as_anchor(desired[parent])),as_anchor(desired[b]));
					const auto local=anchor{add(scale(from.position,1-amount),scale(to.position,amount)),blend_quat(from.rotation,to.rotation,amount)};
					const auto world=compose(as_anchor(solved[parent]),local);solved[b].position=world.position;solved[b].rotation=world.rotation;
				}
			}
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			enabled=dvars::register_bool("vr_physicalGrenades",true,game::DVAR_FLAG_SAVED,"Physical chest grenades, two-hand pin pull and frag cooking");
			const auto& general=settings::grenade_throw_speed;const auto& football=settings::football_throw_speed;
			throw_gain=dvars::register_float(general.name,general.default_value,general.min,general.max,game::DVAR_FLAG_SAVED,"Physical throwable velocity gain");
			football_gain=dvars::register_float(football.name,football.default_value,football.min,football.max,game::DVAR_FLAG_SAVED,"Additional football throw gain");
			ready=native::initialize();
			weapons::native_weapon_sound::initialize();scheduler::loop(play_sounds,scheduler::pipeline::main);
			scheduler::loop(refresh,scheduler::pipeline::main,250ms);fastfiles::on_pre_unload(retire);
			scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);
			command::add("vr_grenade_status",[]{scheduler::once([]{
				std::ostringstream out;out<<"grenades_ready="<<ready<<" takes="<<takes<<" pins="<<pins<<" returns="<<returns<<" throws="<<throws<<" cooks="<<cooks<<" in_hand="<<in_hand<<" spawn_failures="<<failed<<" handoffs="<<handoffs<<" sounds="<<sounds_played<<" missing_sounds="<<sounds_missing<<" reason="<<reason.load()<<'\n';
				{const std::lock_guard lock(mutex);for(unsigned k=0;k<kind_count;++k)out<<"model="<<authored::profiles[k].model<<" ready="<<bool(published.assets[k])<<'\n';}
				for(unsigned i=0;i<2;++i){const auto& s=items[i].value;out<<"slot="<<i<<" weapon="<<s.weapon<<" phase="<<unsigned(s.stage)<<" hand="<<int(s.holder)<<" puller="<<int(s.puller)<<" spent="<<s.spent<<" fuse="<<s.fuse_ms<<" deadline="<<s.deadline<<'\n';}
				const auto native=native::launch_status();
				out<<"throw_gain="<<throw_gain->current.value<<" football_gain="<<football_gain->current.value<<" last_raw_mps="<<last_raw_speed<<" last_scaled_mps="<<last_throw_speed
					<<" native_attempts="<<native.attempts<<" native_spawns="<<native.spawned<<" actor_clearances="<<native.actor_clearances<<" world_clearances="<<native.world_clearances
					<<" requested_units_per_second="<<native.requested_speed<<" admitted_units_per_second="<<native.native_speed<<" obstruction="<<native.obstruction<<'\n';
				const auto text=out.str();console::print_text(console::con_type_info,text);scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-grenades.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
		}
		void pre_destroy()override{alive=false;retire();}
	};
}
REGISTER_COMPONENT(vr::gameplay::grenades::component)
