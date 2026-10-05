#pragma once
#include "component/vr/gameplay/hands/rig_builder.hpp"
#include "component/vr/gameplay/mounted_turret_policy.hpp"
#include "component/vr/gameplay/mounted_turret_pose.hpp"

namespace mounted_turret_tests
{
	struct fixture
	{
		vr::gameplay::mounted::controller control;
		vr::gameplay::mounted::controls geometry;
		vr::controller_input::frame input;
		std::array<vr::controller_input::digital_sampler,2> squeezes,triggers;
		fixture()
		{
			geometry.valid=geometry.calibrated=true;geometry.tracked=3;geometry.units=40;
			geometry.low={-30,-60};geometry.high={45,60};
			geometry.handles={{{-20,5,0},{-20,-5,0}}};geometry.wrists=geometry.handles;
			input.focused=true;input.reference_generation=1;
			input.sampled_at=vr::controller_input::clock::time_point{std::chrono::seconds(1)};
			for (int h=0;h<2;++h) input.grip[h].valid=input.aim[h].valid=true;
		}
		void step(unsigned squeeze=0,unsigned trigger=0)
		{
			++input.sequence;input.sampled_at+=std::chrono::milliseconds(16);
			for (int h=0;h<2;++h)
			{
				input.squeeze[h]=squeezes[h].sample(true,(squeeze&(1u<<h))!=0,input.sampled_at);
				input.trigger[h]=triggers[h].sample(true,(trigger&(1u<<h))!=0,input.sampled_at);
			}
			control.update(input,geometry);
		}
		void takeover() {step();step();step(1);step();step();}
	};
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::mounted;
		{
			const std::array<std::string_view,3> models{"h2_vehicle_blackhawk_minigun_hero_exterior","h2_vehicle_blackhawk_minigun_hero_interior_low","viewhands_player_us_army"};
			check(matches(blackhawk,models) && !matches(suburban,models),"Blackhawk three-model assembly cannot enter the Suburban presentation profile");
			auto foreign=models;foreign[1]="h2_vehicle_blackhawk_minigun_viewhands";
			check(!matches(blackhawk,foreign) && !matches(blackhawk,{models.data(),2}),"impact dummies and incomplete Blackhawk assemblies keep authored presentation");
			check(attached(0x100000,mount_kind::blackhawk) && !attached(0x3002,mount_kind::blackhawk) &&
				!attached(0x100000,mount_kind::suburban),"vehicle and turret native ownership flags remain separate");
			std::array<std::byte,0xb8> native{},local{};native.fill(std::byte{0x5a});
			check(independent_vehicle_pose(native,{-12.f,50.f},local),"native vehicle controller accepts private compact pitch/yaw");
			std::int16_t yaw{},pitch{};std::memcpy(&yaw,local.data()+0x66,2);std::memcpy(&pitch,local.data()+0x6a,2);
			check(std::abs(yaw*(360.f/65536.f)-50.f)<.006f && std::abs(pitch*(360.f/65536.f)+12.f)<.006f,"Blackhawk short-angle signs and units match native limits");
			for(unsigned i=0;i<native.size();++i)if(i!=0x66 && i!=0x67 && i!=0x6a && i!=0x6b)
				check(native[i]==local[i],"vehicle body attitude, controller joints and rotor fields remain native");
			check(!independent_vehicle_pose(native,{0.f,std::numeric_limits<float>::quiet_NaN()},local),"invalid vehicle aim cannot reach native pose fields");
		}
		{
			using namespace vr::gameplay::hands;
			std::array<bone_definition,55> bones{};
			for(unsigned i=0;i<bones.size();++i){bones[i].name="helper";bones[i].parent=i?0:-1;bones[i].bind={{0,0,0,1},{float(i),float(i%3),float(i%5)},2.f};}
			bones[3].parent=2;bones[2].parent=1;bones[38].parent=3;bones[43].parent=38;bones[44].parent=38;
			bones[38].name="tag_turret_base";bones[44].name="tag_player";
			bones[45].name="tag_turret";bones[45].parent=38;bones[49].name="turret_animate_jnt";bones[49].parent=45;
			bones[50].name="tag_barrel";bones[50].parent=49;bones[51].parent=50;bones[52].parent=51;
			bones[54].name="tag_flash";bones[54].parent=51;
			const auto binding=resolve_model_pose(bones,mount_kind::blackhawk);
			check(binding.valid && binding.cover<0 && binding.animated_pitch<0,"Blackhawk resolves one yaw/pitch chain without Suburban cover policies");
			std::array<bone,170> source{},output{};for(unsigned i=0;i<55;++i)source[i]=bones[i].bind;
			source[52].rotation={.3f,0,0,.9539392f};output=source;
			check(project_model_pose(binding,{source.data(),55},{output.data(),55},{-10.f,25.f},true),"Blackhawk visual pose uses its original articulated gun");
			for(unsigned i=0;i<output.size();++i)if(i>=55 || !descendant(int(i),45,binding.layout))
				check(std::memcmp(&source[i],&output[i],sizeof(bone))==0,"aiming preserves helicopter, seat, ammo box, interior and hand source matrices");
			check(length(sub(output[54].position,source[54].position))>1.f,"native muzzle follows the projected Blackhawk aim chain");
			check(length(sub(rotate(conjugate(output[51].rotation),sub(output[52].position,output[51].position)),
				rotate(conjugate(source[51].rotation),sub(source[52].position,source[51].position))))<.001f,"native barrel animation survives the Blackhawk projection");
			bones[44].parent=50;check(!resolve_model_pose(bones,mount_kind::blackhawk).valid,"a gun-parented seat cannot enter the stable Blackhawk profile");
		}
		check(world_scene_flags(0x1002441)==0x1002440 && world_scene_flags(0x1000800)==0x1000800,
			"mounted world-depth removes only the native depth-hack bit");
		{
			fixture sample;sample.takeover();sample.input.turn_active=true;sample.input.turn={0,0};sample.step();sample.step(1);
			const auto original=sample.control.angles;const auto grips=sample.control.gripped;
			float previous{};
			for(int frame=1;frame<=5;++frame)
			{
				auto input=sample.input;input.sequence+=frame;input.sampled_at+=std::chrono::milliseconds(frame*8);
				input.turn={1,0};const auto visual=sample.control.displayed_angles(input,sample.geometry);
				check(visual[1]<previous && sample.control.angles==original && sample.control.gripped==grips,
					"render-frame aim follows each XR sample without changing server aim or grip authority");previous=visual[1];
				check(sample.control.displayed_angles(input,sample.geometry)==visual,"stereo and duplicate queries cannot accumulate predicted aim");
			}
				auto disconnected=sample.input;disconnected.sequence++;disconnected.reference_generation++;
				check(sample.control.displayed_angles(disconnected,sample.geometry)==original,"render aim cannot carry a prediction across recenter");
				auto released=sample.input;released.squeeze[0].down=false;
				check(sample.control.held_hands(sample.input)==1 && !sample.control.held_hands(released) && sample.control.gripped==1,
					"rendered hands release the handle immediately without mutating acknowledged grip ownership");
		}
		for(unsigned held:{0u,1u,3u})
		{
			fixture sample;sample.takeover();sample.input.turn_active=true;sample.step(held);
			sample.control.angles={30.f,10.f};
			std::array<float,2> solved{};controller presentation;
			for(int tick=0;tick<80;++tick)
			{
				sample.step(held);
				// Native eye/barrel convergence adds a persistent correction while
				// the real gun slews toward the requested target at a bounded rate.
				for(unsigned a=0;a<2;++a)
					solved[a]+=std::clamp(sample.control.angles[a]-(a==0?.6f:0.f)-solved[a],-2.f,2.f);
				presentation=sample.control.native_presentation(solved);
			}
			check(sample.control.angles==std::array<float,2>{30.f,10.f} && std::abs(solved[0]-29.4f)<.001f,
				"native convergence cannot accumulate into the user's aim with released, single or dual grips");
			check(presentation.angles==solved && presentation.gripped==held &&
				presentation.displayed_angles(sample.input,sample.geometry)==solved,
				"mounted presentation uses actual native slew/convergence with the acknowledged input history");
			auto next=sample.input;++next.sequence;next.sampled_at+=std::chrono::milliseconds(8);next.turn={0,1};
			const auto visual=presentation.displayed_angles(next,sample.geometry);
			check(visual[0]<solved[0] && presentation.angles==solved && sample.control.angles[0]==30.f,
				"late render input remains relative to the native gun without rewriting its persistent target");
			sample.control.reset({-5.f,4.f});sample.step();
			check(sample.control.angles==std::array<float,2>{-5.f,4.f} && !sample.control.free_hands,
				"a new mount seeds its request from the new native pose and discards the prior target");
		}
		{
			using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
			std::array<bone_definition,48> bones{};
			constexpr std::array parents{-1,0,1,2,2,2,2,2,3,4,7,7,7,12,12,14,14,16,15,17,17,17,17,22,18,18,23,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,44};
			for(int i=0;i<48;++i){bones[i].name="helper";bones[i].parent=parents[i];bones[i].bind.position={float(i),float(i%3),float(i%5)};}
			bones[0].name="tag_cover";bones[1].name="tag_aim_pivot";bones[2].name="j_cover";bones[18].name="tag_aim";
			bones[23].name="tag_aim_animated";bones[25].name="tag_weapon";bones[26].name="j_mg";
			const auto binding=resolve_model_pose(bones);check(binding.valid,"captured turret controller hierarchy admits the presentation profile");
			std::array<bone,48> native{},first{},second{};for(int i=0;i<48;++i)native[i]=bones[i].bind;
			native[0].position={100,200,300};native[47].rotation={.2f,0,0,.979795897f};const auto original=native;
			check(project_model_pose(binding,native,first,{15,35},true) && project_model_pose(binding,native,second,{-20,-40},true),
				"each rendered pitch/yaw is projected from the same native baseline");
			const auto fixed=compose(as_anchor(native[0]),binding.fixed_cover);
			check(length(sub(first[2].position,fixed.position))<.001f && length(sub(second[2].position,fixed.position))<.001f &&
				length(sub(first[26].position,second[26].position))>1 && std::memcmp(native.data(),original.data(),sizeof(native))==0,
				"the lower box follows the stable vehicle root while the gun aims independently and source matrices stay immutable");
			check(std::memcmp(&first[0],&native[0],sizeof(bone))==0 &&
				length(sub(rotate(conjugate(first[44].rotation),sub(first[47].position,first[44].position)),
					rotate(conjugate(native[44].rotation),sub(native[47].position,native[44].position))))<.001f,
				"render aim preserves the vehicle root and native barrel-spin relative placement");
			std::array<bone,48> repeated{};project_model_pose(binding,native,repeated,{15,35},true);
			check(std::memcmp(first.data(),repeated.data(),sizeof(first))==0,"repeated render projections cannot compound native transforms");
			bones[23].parent=-1;check(!resolve_model_pose(bones).valid,"foreign aiming hierarchies cannot receive this profile's bone changes");
		}
		{
			std::array<std::byte,0xb8> native{},local{};native.fill(std::byte{0x5a});
			const auto saved=native;
			check(independent_client_pose(native,{-17.f,83.f},local),"client turret controller accepts independent local aim");
			std::array<float,3> angles{};std::memcpy(angles.data(),local.data()+0x60,sizeof(angles));
			check(native==saved && angles==std::array<float,3>{-17.f,83.f,0.f} && local[0x70]==std::byte{},
				"client aim bypasses the view-angle pointer without mutating the shared pose");
			for (unsigned i=0;i<native.size();++i) if ((i<0x60 || i>=0x6c) && i!=0x70)
				check(local[i]==saved[i],"native attachment fields, controller bone indices and barrel spin are preserved");
			check(!independent_client_pose(native,{std::numeric_limits<float>::infinity(),0.f},local),"nonfinite client aim rejected");
		}
		{
			using namespace vr::gameplay::hands;
			// Live assembly: 48 turret bones followed by 68 US Army hand bones.
			// Semantic indices/parents are witnessed; poses below are synthetic.
			std::array<bone_definition,68> definitions{};
			for (int b=0;b<68;++b) definitions[b]={"helper",b ? 0 : -1};
			definitions[7]={"tag_torso",0};definitions[8]={"j_shoulder_le",7};definitions[9]={"j_shoulder_ri",7};
			definitions[13]={"tag_weapon",7};definitions[14]={"j_elbow_le",8};definitions[15]={"j_elbow_ri",9};
			definitions[24]={"j_wrist_le",14};definitions[25]={"j_wrist_ri",15};definitions[28]={"finger",24};
			const model_definition hands_model{"viewhands_player_us_army",0,68};
			const auto resolved=resolve_rig({&hands_model,1},definitions,rig_kind::hands_only);
			check(!resolved.rejection,"attached hand submodel resolves independently of the preceding turret model");
			if (!resolved.rejection)
			{
				std::array<bone,116> source{};for (auto& b:source) b={{0,0,0,1},{0,0,0},2};
				for (int h=0;h<2;++h)
				{
					const auto a=resolved.layout.arms[h];const float side=h ? -2.f : 2.f;
					source[48+a.shoulder].position={0,side,0};source[48+a.elbow].position={3,side,-4};source[48+a.wrist].position={6,side,0};
				}
				const auto original=source;
				std::array<bone,68> solved{};std::array<bool,2> limited{};
				const std::array<anchor,2> targets{{{{4,4,1}},{{5,-3,1}}}};
				const std::array<vec,2> shoulders{{{-1,3,-2},{-1,-3,-2}}};
				const std::array<vec,3> axis{{{1,0,0},{0,1,0},{0,0,1}}};
				check(solve_arms(resolved.layout,{source.data()+48,68},targets,shoulders,axis,solved,limited),"turret's own attached arms reach free controller targets");
				for (int b=0;b<68;++b) if (arm_bone(resolved.layout,b)) source[48+b]=solved[b];
				for (int b=0;b<116;++b) if (b<48 || !arm_bone(resolved.layout,b-48))
					check(std::memcmp(&source[b],&original[b],sizeof(bone))==0,"freeing attached arms preserves gun, seat and non-arm tags exactly");
				for (int h=0;h<2;++h) check(length(sub(source[48+resolved.layout.arms[h].wrist].position,targets[h].position))<.002f,
					"free turret wrist is no longer left at its authored handle");
				std::array<bone,68> bind{},animated{},rest{},stable{};
				std::copy_n(original.begin()+48,68,bind.begin());
				bind[28].position=add(bind[24].position,{1,0,0});
				check(seed_arm_pose(resolved.layout,bind,{}, {},bind,0,rest) &&
					solve_arms(resolved.layout,rest,targets,shoulders,axis,stable,limited),"mounted free arms reuse the stable model bind source");
				for(int frame=0;frame<4;++frame)
				{
					for(unsigned b=0;b<bind.size();++b)
					{animated[b]=bind[b];animated[b].position=add(scale(bind[b].position,.3f+frame*.15f),{float(frame*20),10,-15});}
					check(seed_arm_pose(resolved.layout,bind,{}, {},animated,0,rest) &&
						solve_arms(resolved.layout,rest,targets,shoulders,axis,solved,limited),"changing native turret animation cannot shorten the free-arm solve");
					for(int h=0;h<2;++h)
					{
						const auto wrist=resolved.layout.arms[h].wrist;
						check(length(sub(solved[wrist].position,targets[h].position))<.002f &&
							length(sub(solved[wrist].position,stable[wrist].position))<.002f,"free wrists stay on the current tracked targets through moving gun animations");
					}
				}
				animated[28].position=add(animated[24].position,{.2f,.3f,.4f});
				check(seed_arm_pose(resolved.layout,bind,{}, {},animated,1,rest) &&
					length(sub(sub(rest[28].position,rest[24].position),{.2f,.3f,.4f}))<.001f,
					"a gripped hand retains its native finger articulation on the stable arm source");
			}
		}
		check(attached(0x3002) && !attached(2),"native mounted flags distinguish turret ownership from on-foot movement");
		fixture f;f.step(3,3);f.step(3,3);f.step();
		check(!f.control.free_hands && !f.control.gripped,"buttons held on entry cannot take over or grab on their initial release");
		f.step();f.step(1);f.step();
		check(f.control.free_hands && !f.control.gripped && !f.control.firing(f.input),"a fresh complete squeeze takes over without a simultaneous grab or shot");
		f.step();f.geometry.wrists[0]=f.geometry.handles[1];f.step(1);
		check(!f.control.gripped,"left hand at right handle cannot grab the opposite side");
		f.step();f.geometry.wrists=f.geometry.handles;f.step(1,1);
		check(f.control.gripped==1 && !f.control.firing(f.input),"trigger already held when grabbing must return to neutral before firing");
		f.step(1,0);f.step(1,2);
		check(!f.control.firing(f.input),"ungripped hand trigger cannot fire the turret");
		f.step(1,3);check(f.control.firing(f.input),"gripped left hand can fire even while the other trigger is held");
		f.step(1,0);f.step(3,0);f.step(3,3);
		check(f.control.gripped==3 && f.control.firing(f.input),"both hands can independently grab their own handles");
		f.step(3,2);check(f.control.firing(f.input),"releasing left trigger does not interrupt right hand fire");
		f.step(3,0);check(!f.control.firing(f.input),"both triggers released ends firing");
		for (unsigned grips=0;grips<4;++grips) for (unsigned triggers=0;triggers<4;++triggers)
		{
			fixture p;p.takeover();p.step(grips);p.step(grips,triggers);
			check(p.control.firing(p.input)==bool(grips&triggers),"turret fire truth table requires a gripping hand's own trigger");
		}
		f.geometry.tracked=1;f.step(3,2);
		check(f.control.gripped==1 && !f.control.firing(f.input),"losing right tracking releases only the right grip and its fire");
		f.geometry.tracked=3;f.step(3,2);check(f.control.gripped==1,"tracking recovery while squeezed cannot reacquire a lost grip");
		f.input.focused=false;f.step(3,3);
		check(!f.control.gripped && !f.control.free_hands && !f.control.firing(f.input),"focus loss cancels takeover, both grips and fire");
		fixture r;r.takeover();r.step(1);++r.input.reference_generation;r.step(1,1);
		check(!r.control.free_hands && !r.control.gripped,"recenter requires another neutral takeover instead of retaining world-space handles");
		fixture stale;stale.takeover();stale.step(1);stale.input.sampled_at+=std::chrono::seconds(1);stale.step(1,1);
		check(!stale.control.gripped && !stale.control.free_hands,"stale stream cannot resume a held turret shot");
		fixture no_pose;no_pose.geometry.calibrated=false;no_pose.takeover();
		check(!no_pose.control.free_hands,"uncalibrated button tags cannot be mistaken for authored wrist poses");
		fixture aim;aim.takeover();aim.input.turn_active=true;aim.input.turn={1,1};aim.step();
		check(aim.control.angles==std::array<float,2>{},"stick already deflected on activation must center before aiming");
		aim.input.turn={0,0};aim.step();aim.input.turn={1,1};aim.step();
		check(aim.control.angles[0]<0 && aim.control.angles[1]<0,"right stick independently controls pitch and yaw with native handedness");
		for (int n=0;n<300;++n) aim.step();
		check(aim.control.angles==aim.geometry.low,"continuous stick input respects both native lower aim limits");
		aim.input.turn={-1,-1};for (int n=0;n<400;++n) aim.step();
		check(aim.control.angles==aim.geometry.high,"continuous stick input respects both native upper aim limits");
		fixture round;round.geometry.low[1]=-180;round.geometry.high[1]=180;round.takeover();
		round.input.turn_active=true;round.step();round.input.turn={-1,0};
		for (int n=0;n<140;++n) round.step();
		check(round.control.angles[1]<-150 && round.control.angles[1]>-180,"full-circle native yaw wraps through 180 instead of sticking at the seam");
		fixture physical;physical.takeover();physical.step(3);
		for (auto& wrist:physical.geometry.wrists) wrist[2]+=2;
		physical.step(3);check(std::abs(physical.control.angles[0])>1,"two gripping hands contribute physical pitch input");
		physical.geometry.wrists[0]={0,0,0};physical.step(3);
		check(physical.control.gripped==3,"crossing an aim pivot skips invalid aim without dropping a held grip");
		fixture reach;reach.takeover();reach.step(1);
		reach.geometry.wrists[0]={-200,50,0};reach.step(1,1);
		check(reach.control.gripped==1 && reach.control.firing(reach.input),
			"moving far beyond the acquisition radius retains the held grip and its trigger");
		reach.geometry.wrists[0]={200,-50,0};reach.step(1,1);
		check(reach.control.gripped==1 && reach.control.firing(reach.input),
			"discontinuous physical aim is ignored without dropping a valid squeeze");
		reach.step();check(!reach.control.gripped && !reach.control.firing(reach.input),
			"squeeze release still drops the grip at any distance");
		reach.step(1);check(!reach.control.gripped,"a distant hand cannot acquire a new grip");
		physical.control.reset();check(!physical.control.free_hands && !physical.control.gripped,"dismount/deletion/checkpoint reset discards prior turret gestures");
		fixture joints;
		// Witnessed Team Player geometry: wrists are FORWARD of the base yaw
		// pivot, but BEHIND the raised pitch pivot. A shared pivot reverses yaw.
		joints.geometry.pivot={25.4f,0.f,15.45f};joints.geometry.yaw_pivot={0,0,0};
		joints.geometry.handles={{{7,4,15},{7,-4,15}}};joints.geometry.wrists=joints.geometry.handles;
		joints.takeover();joints.step(3);
		for (auto& wrist:joints.geometry.wrists) wrist[1]+=2;
		joints.step(3);
		check(joints.control.gripped==3 && joints.control.angles[1]>10.f,
			"leftward hand translation steers left about the actual base yaw joint");
		const auto before_pivot_motion=joints.control.angles;
		joints.geometry.pivot[0]+=3;joints.geometry.pivot[1]+=4;joints.geometry.yaw_pivot[1]+=1;
		joints.step(3);
		check(joints.control.angles==before_pivot_motion,
			"moving gun joints cannot feed aim back into stationary controllers");
	}
}
