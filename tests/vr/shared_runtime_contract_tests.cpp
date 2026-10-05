#include "callback_registration_tests.hpp"
#include "component/scene_submission_pool.hpp"
#include "component/scene_pose_match.hpp"
#include "component/vr/gameplay/weapon_runtime_lifecycle.hpp"
#include "component/vr/gameplay/weapon_holding.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include <cstring>
#include <iostream>

int main()
{
	int failures{};
	const auto check=[&](bool value,const char* message){if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
	callback_registration_tests::run(check);
	using pool=scene_models::submission_pool<unsigned,2,2>;
	pool submissions;const auto now=pool::clock::time_point{std::chrono::seconds(10)};
	const auto first=submissions.acquire(11,now),second=submissions.acquire(22,now);
	check(first && second,"two independent immutable submissions admitted");
	check(!submissions.acquire(33,now),"overload cannot overwrite a still eligible queued submission");
	std::array<std::byte,0x70> entry{};
	const auto place_handle=[&](unsigned short* handle){std::memcpy(entry.data()+0x68,&handle,sizeof(handle));};
	place_handle(submissions.handle(*first,1));auto lease=submissions.lookup(entry.data());
	check(lease && lease->payload==11 && lease->part==1 && pool::fresh(*lease,now),"all pieces retain one frozen payload and submission clock");
	lease->payload=99;
	check(submissions.lookup(entry.data())->payload==11,"consumer copies cannot mutate producer records");
	check(!pool::fresh(*lease,now-std::chrono::milliseconds(1)) && !pool::fresh(*lease,now+pool::max_age+std::chrono::milliseconds(1)),"clock reversal and expired snapshots fail closed");
	unsigned short foreign{};place_handle(&foreign);
	check(!submissions.lookup(entry.data()),"foreign native lighting pointers are not claimed");
	check(!submissions.handle(2) && !submissions.handle(0,2),"invalid slot and piece never return a native handle");
	submissions.clear_after_drain();place_handle(submissions.handle(*first,0));
	check(!pool::fresh(*submissions.lookup(entry.data()),now),"drained retirement invalidates old metadata");
	struct pose_record{std::uintptr_t object{},matrices{};unsigned epoch{},owner{};};
	const std::array<pose_record,2> poses{{{1,2,3,7},{1,2,3,8}}};pose_record selected;
	check(scene_models::latest_skeleton_pose(poses,2,poses[1],[](const auto& p)noexcept{return p.owner==7;},selected) && selected.owner==7,
		"matching native pointers and epoch cannot replace the caller's physical identity");
	using namespace vr::gameplay::weapons;
	struct presentation
	{
		bool active{},fault{};hold owner{};unsigned watermark{},revision{};
		void resume_transfer() noexcept{++revision;}
	};
	struct instance{presentation view;};
	std::array<instance,2> inventory{};presentation saved{true,false,{7,1,vr::hand::right},41,2};saved.owner.instance_generation=8;
	check(runtime_lifecycle::restore_transfer(inventory,saved,true,[](auto&){}),"accepted native transfer restores the same physical instance");
	check(inventory[0].view.watermark==41 && inventory[0].view.revision==3,"transfer preserves already consumed event watermarks");
	check(!runtime_lifecycle::restore_transfer(inventory,saved,false,[](auto&){} ) && inventory[0].view.fault,"rejected native transfer retains a faulted identity instead of reimporting state");
	runtime_lifecycle::ownership_snapshot ownership;ownership.independent=true;ownership.instances[0]=saved.owner.id();
	check(ownership.accepts(saved.owner.id()) && !ownership.accepts({7,9}) && !ownership.accepts({}),"ownership distinguishes reused definitions and rejects empty identities");
	namespace hi=vr::gameplay::hand_interaction;
	check(bool(hi::world_object(3,0)) && !hi::world_object(4000,0),"initial world entity generations retain their own validity boundary");
	check(bool(hi::head_gesture_object(9)) && !hi::head_gesture_object(0),"head gestures have no firearm token but require a reference generation");
	std::cout<<"shared runtime contract failures="<<failures<<'\n';return failures?1:0;
}
