#pragma once
#include "component/vr/gameplay/native_ammunition_storage.hpp"
#include <array>

namespace native_ammunition_storage_tests
{
	template<class Check> void run(Check check)
	{
		namespace s=vr::gameplay::weapons::native_ammunition::storage;
		using buffer=std::array<std::byte,s::extent>;
		const auto put=[](buffer& bytes,std::size_t at,std::uint64_t key,int count) {
			std::memcpy(bytes.data()+at,&key,8); std::memcpy(bytes.data()+at+8,&count,4);
		};
		buffer bytes{};
		// Live Shepherd revolver witness: owned token 129 maps to key 22;
		// the reserve cell exists at zero, while the clip cell is absent.
		put(bytes,0x4a4,20,7); put(bytes,0x3f0,20,21); put(bytes,0x3f0+4*12,22,0);
		const auto sparse=bytes;
		auto v=s::observe(bytes,22,22);
		check(v.valid() && !v.clip.at && v.clip.count==0 && v.reserve.count==0,
			"owned revolver without clip record is valid and empty");
		check(s::commit(bytes,22,22,0,0,0,0) && bytes==sparse,"empty mechanical action creates no ammunition or cells");
		put(bytes,0x3f0+4*12,22,18);
		check(s::commit(bytes,22,22,0,18,6,12),"first reload allocates missing clip from reserve");
		v=s::observe(bytes,22,22);
		const auto pistol=s::observe(bytes,20,20);
		check(v.valid() && v.clip.count==6 && v.reserve.count==12 && pistol.clip.count==7 && pistol.reserve.count==21,
			"lazy allocation preserves other weapon and ammunition budget");
		const auto loaded=bytes;
		check(!s::commit(bytes,22,22,0,18,6,12) && bytes==loaded,"stale comparison cannot duplicate reload");
		bytes={}; put(bytes,0x4a4,22,6);
		check(s::commit(bytes,22,22,6,0,0,6),"unloading into absent reserve allocates reserve");
		v=s::observe(bytes,22,22);
		check(v.clip.count==0 && v.reserve.count==6,"unloading conserves ammunition");
		bytes={}; put(bytes,0x4a4,0xaabbcc0000000016ull,6);
		put(bytes,0x3f0,0xaabb000000000016ull,12);
		check(s::observe(bytes,22,22).valid() && s::observe(bytes,22,22).clip.count==6,
			"native key padding does not change identity");
		put(bytes,0x4a4+24,22,1);
		const auto duplicate=bytes;
		check(!s::observe(bytes,22,22).valid() && !s::commit(bytes,22,22,6,12,5,12) && bytes==duplicate,
			"duplicate clip identity rejects observation and writes");
		bytes={}; put(bytes,0x3f0,22,1); put(bytes,0x3f0+12,0xffff000000000016ull,2);
		check(!s::observe(bytes,22,22).valid(),"duplicate reserve identity rejects observation");
		bytes={}; put(bytes,0x4a4,0x100000016ull,3); put(bytes,0x3f0,0x10000000016ull,4);
		v=s::observe(bytes,22,22);
		check(v.valid() && v.clip.count==0 && v.reserve.count==0,
			"alternate clip kind and reserve subkind remain separate identities");
		bytes={}; put(bytes,0x3f0,22,18);
		for (std::size_t i=0;i<15;++i) put(bytes,0x4a4+i*24,100+i,1);
		const auto full_clip=bytes;
		check(!s::commit(bytes,22,22,0,18,6,12) && bytes==full_clip,
			"full clip table cannot overwrite first weapon or debit reserve");
		bytes={};
		for (std::size_t i=0;i<15;++i) put(bytes,0x3f0+i*12,100+i,1);
		const auto full_reserve=bytes;
		check(!s::commit(bytes,22,22,0,0,6,6) && bytes==full_reserve,
			"both destinations are preflighted before allocating either");
		check(!s::commit_reserve_pair(bytes,{{{100,1,0},{22,0,6}}}) && bytes==full_reserve,
			"full reserve table rejects an exchange before debiting the existing pool");
		bytes={};put(bytes,0x3f0,22,3);const auto before_exchange=bytes;
		check(!s::commit_reserve_pair(bytes,{{{22,3,2},{0xffff000000000016ull,3,4}}}) && bytes==before_exchange,
			"aliased reserve keys cannot masquerade as two ammunition supplies");
		check(s::commit_reserve_pair(bytes,{{{22,3,2},{23,0,7}}}) && s::find(bytes,22,false).count==2 && s::find(bytes,23,false).count==7,
			"exchange allocates a missing refund pool without changing the other identity");
		bytes={}; put(bytes,0x4a4,22,-1);
		check(!s::observe(bytes,22,22).valid(),"negative native loaded count remains invalid");
		bytes={}; put(bytes,0x3f0,22,1000001);
		check(!s::observe(bytes,22,22).valid(),"excess native reserve remains invalid");
		check(!s::observe(bytes,0,22).valid() && !s::observe(bytes,22,0).valid(),"empty keys cannot match vacant cells");
		check(!s::observe(std::span<const std::byte>(bytes).first(100),22,22).valid(),"truncated native store rejects before reading");
	}
}
