#pragma once
#include "component/vr/gameplay/hand_attachment_pose.hpp"
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
		a::publish(first);a::begin_record(record.data());
		const auto skinned=a::before_skin(first_object.data(),first_bones.data());a::after_skin(skinned,1,record.data());
		auto second=first;second.object=reinterpret_cast<std::uintptr_t>(second_object.data());second.matrices=reinterpret_cast<std::uintptr_t>(second_bones.data());
		second.wrists=second_bones;second.reload_items={0,333};a::publish(second);
		a::after_skin(a::before_skin(second_object.data(),second_bones.data()),1,record.data());
		a::solved selected;
		check(a::for_record(record.data(),camera,selected,0,111) && selected.object==first.object && selected.wrists[0].position==first_bones[0].position,
			"independent item selects the record that actually posed its hand, even after a later hidden-hand skin");
		check(a::for_record(record.data(),camera,selected,1,333) && selected.object==second.object,"each hand follows its own container pose revision");
		check(a::for_record(record.data(),camera,selected) && selected.object==second.object,"existing equipment record selection remains unchanged");
		check(!a::for_record(record.data(),camera,selected,0,333) && !a::for_record(record.data(),camera,selected,1,999) &&
			!a::for_record(record.data(),camera,selected,2,111) && !a::for_record(record.data(),camera,selected,0,0),"foreign, missing and invalid hand-item revisions cannot borrow a native wrist");
		a::begin_record(record.data());check(!a::for_record(record.data(),camera,selected,0,111),"record reuse cannot replay a previous caught item");
		a::clear_after_drain();
	}
}
