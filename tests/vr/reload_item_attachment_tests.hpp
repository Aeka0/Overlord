#pragma once
#include "component/vr/gameplay/hands/attachment_pose.hpp"
#include "component/vr/engine_stereo_view.hpp"
#include <cstring>

namespace reload_item_attachment_tests
{
	template<class Check> void run(Check check)
	{
		namespace a=vr::gameplay::hands::attachments;using vr::gameplay::hands::bone;
		a::clear_after_drain();
		std::array<std::byte,256> first_object{},second_object{};
		std::array<bone,2> first_bones{},second_bones{};
		first_bones[0].position={1,2,3};first_bones[1].position={4,5,6};second_bones[1].position={10,11,12};
		const std::uint32_t epoch=17;std::memcpy(first_object.data()+0xb0,&epoch,sizeof(epoch));std::memcpy(second_object.data()+0xb0,&epoch,sizeof(epoch));
		std::array<float,12> camera{};camera[0]=13;
		std::array<std::byte,vr::engine_stereo_view::h2_view_origin_offset+sizeof(camera)> record{};
		std::memcpy(record.data()+vr::engine_stereo_view::h2_view_origin_offset,camera.data(),sizeof(camera));
		a::solved first;first.object=reinterpret_cast<std::uintptr_t>(first_object.data());first.matrices=reinterpret_cast<std::uintptr_t>(first_bones.data());
		first.epoch=epoch;first.indices={0,1};first.wrists=first_bones;first.reference=7;first.sequence=8;first.at=vr::controller_input::clock::now();first.reload_items={111,222};
		first.lease=41;first.render_origin={100,200,300};first.knife_hands=1;
		a::publish(first);a::begin_record(record.data());
		a::solved selected;
		check(a::for_current(7,41,selected,0) && selected.object==first.object && selected.render_origin==first.render_origin,
			"skeletal scene submission consumes committed hands before native viewmodel skinning");
		const auto skinned=a::before_skin(first_object.data(),first_bones.data());a::after_skin(skinned,1,record.data());
		auto second=first;second.object=reinterpret_cast<std::uintptr_t>(second_object.data());second.matrices=reinterpret_cast<std::uintptr_t>(second_bones.data());
		second.wrists=second_bones;second.reload_items={0,333};second.render_origin={400,500,600};second.knife_hands=0;a::publish(second);
		a::after_skin(a::before_skin(second_object.data(),second_bones.data()),1,record.data());
		check(a::for_record(record.data(),camera,selected,0,111) && selected.object==first.object && selected.wrists[0].position==first_bones[0].position,
			"independent item selects the record that actually posed its hand, even after a later hidden-hand skin");
		check(a::for_record(record.data(),camera,selected,1,333) && selected.object==second.object,"each hand follows its own container pose revision");
		check(a::for_record(record.data(),camera,selected) && selected.object==second.object,"existing equipment record selection remains unchanged");
		check(!a::for_record(record.data(),camera,selected,0,333) && !a::for_record(record.data(),camera,selected,1,999) &&
			!a::for_record(record.data(),camera,selected,2,111) && !a::for_record(record.data(),camera,selected,0,0),"foreign, missing and invalid hand-item revisions cannot borrow a native wrist");
		check(a::for_current(7,41,selected,0) && selected.object==first.object && selected.render_origin==first.render_origin,
			"skeletal equipment selects its exact committed lease and copied origin rather than a later unrelated hand");
		check(!a::for_current(8,41,selected,0) && !a::for_current(7,43,selected,0),
			"skeletal equipment cannot cross tracking references or returned tool leases");
		first_bones[0].position[0]+=1;
		check(!a::for_current(7,41,selected,0),"skeletal equipment rejects a skinned snapshot after its actual wrist buffer changes");
		first_bones[0]=first.wrists[0];
		const auto next_epoch=epoch+1;std::memcpy(first_object.data()+0xb0,&next_epoch,sizeof(next_epoch));
		check(!a::for_current(7,41,selected,0),"skeletal equipment rejects DObj epoch reuse even if wrist coordinates match");
		std::memcpy(first_object.data()+0xb0,&epoch,sizeof(epoch));
		a::begin_record(record.data());check(!a::for_record(record.data(),camera,selected,0,111),"record reuse cannot replay a previous caught item");
		check(a::for_current(7,41,selected,0),"viewmodel record reuse cannot erase a still-current committed skeleton");
		auto hidden=first;hidden.knife_hands=0;a::publish(hidden);
		check(!a::for_current(7,41,selected,0),"a newer hidden-hand solve supersedes an identical earlier knife wrist");
		hidden.knife_hands=1;hidden.lease=42;a::publish(hidden);
		check(!a::for_current(7,41,selected,0),"a newer lease cannot expose an older matching wrist");
		a::clear_after_drain();
	}
}
