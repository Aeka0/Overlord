#pragma once
#include "component/vr/gameplay/sequences/airport_opening.hpp"

template<class Check> void airport_opening_tests(Check check)
{
    using namespace vr::gameplay::sequences::airport::opening;
    constexpr std::uint32_t m240=19,prop=77;
    std::array<std::byte,220> code{};
    const auto put=[&](std::size_t at,std::initializer_list<unsigned char> bytes)
    {for(const auto byte:bytes)code[at++]=std::byte{byte};};
    put(0,{0x32,0x89,0x79,0x2e});put(code.size()-9,{0x89,0xa0,1,0x2e});put(code.size()-2,{0x6a,0x34});
    const auto statement=[&](std::size_t at,std::uint32_t name,std::uint16_t method)
    {
        code[at]=std::byte{0x53};std::memcpy(code.data()+at+1,&name,4);
        put(at+5,{0x55,0x1a,0x03,0xaa});std::memcpy(code.data()+at+9,&method,2);code[at+11]=std::byte{0x6a};
    };
    statement(16,m240,0x831a);statement(40,prop,0x8319);statement(52,prop,0x8320);
    statement(80,prop,0x831a);statement(108,m240,0x8319);statement(140,m240,0x833d);
    put(152,{0xa0,100});statement(154,m240,0x8301);code[162]=std::byte{0xab};statement(166,m240,0x8320);
    // Other weapons and their ammo retain their native route.
    statement(28,123,0x831a);statement(120,123,0x833d);
    const auto found=inspect(code,m240,prop);
    check(found && found->replacements==std::array<std::size_t,8>{16,40,52,80,108,140,152,166} && found->finish==219,
        "airport limits redirection to the native replacement and its ammo initialization");
    check(found && found->resume(1)==64 && found->resume(2)==64 && found->resume(4)==120 && found->resume(5)==178,
        "adjacent replacement statements are coalesced without resetting retained ammunition");
    for(std::size_t length=0;length<code.size();++length)
        check(!inspect({code.data(),length},m240,prop),"incomplete airport script is rejected before any hooks");
    check(!inspect(code,m240,m240) && !inspect(code,0,prop),"invalid native weapon string identities reject adaptation");
    const auto original=code;
    statement(64,prop,0x8319);check(!inspect(code,m240,prop),"extra native replacement rejects ambiguous layout");
    code=original;statement(52,prop,0x831a);check(!inspect(code,m240,prop),"changed native operation rejects layout");
    code=original;code[21]=std::byte{0x41};check(!inspect(code,m240,prop),"non-player method cannot become a replacement site");
    code=original;code[153]=std::byte{99};check(!inspect(code,m240,prop),"changed native ammo initialization rejects layout");
}
