#pragma once
#include "component/vr/gameplay/native_fx_checkpoint_policy.hpp"

template<class Check> void native_fx_checkpoint_tests(const Check& check)
{
	using namespace vr::gameplay::native_fx::checkpoint;
	check(valid_effect_handle(0x4d58) && valid_effect_handle(0x8fc0) && valid_effect_handle(0x9fec) &&
		!valid_effect_handle(0x9fff) && !valid_effect_handle(0xa000) && !valid_effect_handle(1),
		"restored effect handles stay aligned and within the captured 2048-slot native pool");
	const auto followed=name(variant::followed,"fx/flare",0x800000ff);
	const auto parsed=parse(followed);
	check(parsed && parsed->kind==variant::followed && parsed->attached==0x800000ff && parsed->source=="fx/flare",
		"followed checkpoint identity restores all 32 attached-element bits");
	check(!parse("__overlord_fx/follow/0000000z/fx/flare") && !parse("__overlord_fx/world/") &&
		!parse("__overlord_fx/follow/0000000/fx/flare") && !parse("__overlord_fx/other/fx/flare") &&
		name(variant::world,std::string(256,'x')).empty() &&
		name(variant::world,"__overlord_fx/world/fx/flare").empty(),
		"malformed, recursive and oversized checkpoint identities reject without native fallback");
	std::array<std::string,variant_capacity+1> names;
	std::array<game::FxEffectDef,variant_capacity+1> effects{};
	std::array<game::FxEffectDef*,variant_capacity+1> pointers{};
	for(unsigned i=0;i<effects.size();++i)
	{
		names[i]=name(i==variant_capacity-1?variant::followed:variant::world,"fx/"+std::to_string(i),1);
		effects[i].name=names[i].c_str();pointers[i]=&effects[i];
	}
	registry dictionary;
	check(!dictionary.publish(pointers) && !dictionary.size(),"too many private definitions reject atomically before publishing any pointer");
	check(dictionary.publish({pointers.data(),variant_capacity}) && dictionary.publish({pointers.data(),variant_capacity}) &&
		dictionary.size()==variant_capacity,"bounded registration is idempotent for reused in-flight definitions");
	auto duplicate=effects[0];const std::array<game::FxEffectDef*,1> duplicates{&duplicate};
	check(!dictionary.publish(duplicates) && dictionary.size()==variant_capacity,"same checkpoint name cannot alias two retained definitions");
	std::array<game::FxEffectDef*,native_capacity> native{};native[0]=&duplicate;
	std::size_t count=native_capacity-variant_capacity+1;
	check(!dictionary.append(native,count) && count==native_capacity-variant_capacity+1 && native[0]==&duplicate,
		"native dictionary exhaustion leaves existing entries and count unchanged");
	dictionary.remove(variant::world);
	check(dictionary.size()==1,"zone retirement removes only the owning presentation cache");
	dictionary.remove(variant::followed);check(!dictionary.size(),"all presentation pointers retire before descriptor storage");

	std::array<std::byte,0xe8> tail{};
	const auto write=[&]<class T>(std::size_t at,T value){std::memcpy(tail.data()+at,&value,sizeof(T));};
	write(8,0x04008001u);write(0x34,0x4d58u);
	for(unsigned at=0xc;at<0x28;at+=4)write(at,0xffffffffu);
	write(0x28,static_cast<unsigned short>(0xffff));write(0x4e,static_cast<unsigned short>(0xffff));
	check(empty_orphan_tail(tail,0x4d58),"observed legacy checkpoint's empty single-reference root tail admits native retirement");
	write(0x10,0u);check(!empty_orphan_tail(tail,0x4d58),"unmapped effect with live particles cannot be discarded");write(0x10,0xffffffffu);
	write(8,0x04008002u);check(!empty_orphan_tail(tail,0x4d58),"shared native references reject empty-tail recovery");write(8,0x04008001u);
	write(0x1c,0u);check(!empty_orphan_tail(tail,0x4d58),"native trail ownership blocks orphan retirement");write(0x1c,0xffffffffu);
	write(0x4e,static_cast<unsigned short>(0));check(!empty_orphan_tail(tail,0x4d58),"native bolt ownership blocks orphan retirement");
	write(0x4e,static_cast<unsigned short>(0xffff));
	check(!empty_orphan_tail(tail,0x8fc0) && !empty_orphan_tail(std::span(tail).first(8),0x4d58),
		"wrong owner and truncated effects reject before field reads");
}
