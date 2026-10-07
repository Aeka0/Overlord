#pragma once
#include "component/vr/gameplay/native_fx_world_space.hpp"
#include <cstring>
#include <limits>

template<class Check> void native_fx_world_space_tests(const Check& check)
{
	using vr::gameplay::native_fx::world_space_cache;
	game::XModel model{};
	game::FxElemVelStateSample velocity{};
	std::array<game::FxElemDef,3> elements{};
	game::FxElemDef child_element{};
	game::FxEffectDef root{},child{};
	root.elemDefCountOneShot=3;root.elemDefs=elements.data();root.flags=0x800;root.totalSize=1234;
	child.elemDefCountOneShot=1;child.elemDefs=&child_element;
	child_element.flags=game::FX_ELEM_DRAW_WITH_VIEWMODEL | game::FX_ELEM_HAS_GRAVITY;
	child_element.elemType=game::FX_ELEM_TYPE_MODEL;child_element.visualCount=1;
	child_element.visuals.instance.model=&model;
	auto& shell=elements[0];shell=child_element;
	shell.flags|=game::FX_ELEM_USE_MODEL_PHYSICS | game::FX_ELEM_RUN_RELATIVE_TO_SPAWN;
	shell.spawnOrigin[0]={2.f,3.f};shell.gravity={1.f,2.f};shell.velSamples=&velocity;
	shell.effectOnImpact.handle=&child;shell.effectOnDeath.handle=&child;shell.effectEmitted.handle=&child;
	std::array<game::FxElemVisuals,2> runners{};
	runners[0].effectDef.handle=&child;runners[1].effectDef.handle=&root;
	elements[1].elemType=game::FX_ELEM_TYPE_RUNNER;elements[1].visualCount=2;
	elements[1].visuals.array=runners.data();elements[1].flags=game::FX_ELEM_DRAW_WITH_VIEWMODEL;
	elements[2].elemType=game::FX_ELEM_TYPE_RUNNER;elements[2].visualCount=1;
	elements[2].visuals.instance.effectDef.handle=&child;
	const auto original_elements=elements;
	const auto original_root=root,original_child=child;
	world_space_cache cache;
	auto* world=cache.get(&root);
	check(world && world!=&root && cache.size()==2,"shell graph gets private stable definitions");
	if(!world)return;
	auto* world_child=world->elemDefs[0].effectOnImpact.handle;
	check(world_child && world_child!=&child && world_child==world->elemDefs[0].effectOnDeath.handle &&
		world_child==world->elemDefs[0].effectEmitted.handle,"impact/death/emission share one world-depth child");
	check(world->elemDefs[1].visuals.array!=runners.data() &&
		world->elemDefs[1].visuals.array[0].effectDef.handle==world_child &&
		world->elemDefs[1].visuals.array[1].effectDef.handle==world &&
		world->elemDefs[2].visuals.instance.effectDef.handle==world_child,"runner arrays, single runners and cycles stay within private graph");
	auto expected=shell;expected.flags&=~game::FX_ELEM_DRAW_WITH_VIEWMODEL;
	expected.effectOnImpact.handle=expected.effectOnDeath.handle=expected.effectEmitted.handle=world_child;
	check(std::memcmp(&expected,&world->elemDefs[0],sizeof(expected))==0 &&
		!(world_child->elemDefs[0].flags&game::FX_ELEM_DRAW_WITH_VIEWMODEL),
		"world shells preserve model, physics, motion and every non-depth field");
	check(world->flags==root.flags && world->totalSize==root.totalSize &&
		std::memcmp(elements.data(),original_elements.data(),sizeof(elements))==0 &&
		std::memcmp(&root,&original_root,sizeof(root))==0 && std::memcmp(&child,&original_child,sizeof(child))==0 &&
		runners[0].effectDef.handle==&child && runners[1].effectDef.handle==&root,
		"shared native FX definitions and effect-level flags remain untouched");
	check(cache.get(&root)==world && cache.size()==2,"repeated shots reuse retained immutable shell graph");
	std::array<game::FxEffectDef,127> more{};
	for(unsigned i=0;i<126;++i)check(cache.get(&more[i])!=nullptr,"bounded shell cache admission");
	check(!cache.get(&more.back()) && cache.get(&root)==world && world->elemDefs[0].visuals.instance.model==&model,
		"capacity exhaustion never evicts in-flight FX descriptors");
	cache.clear();check(!cache.size(),"native asset drain retires shell descriptors");
	child.elemDefCountOneShot=-1;
	check(!cache.get(&root) && !cache.size(),"invalid child rejects complete graph without partial publication");
	child=original_child;
	check(cache.get(&root) && cache.size()==2,"failed graph construction does not poison the cache");
	cache.clear();
	game::FxEffectDef invalid{};invalid.elemDefCountOneShot=1;
	check(!cache.get(&invalid),"missing element storage rejects shell FX");
	invalid.elemDefs=elements.data();invalid.elemDefCountOneShot=std::numeric_limits<int>::max();
	check(!cache.get(&invalid),"oversized counts reject before arithmetic or allocation");
	invalid.elemDefCountOneShot=33;invalid.elemDefCountLooping=32;
	check(!cache.get(&invalid),"combined element count respects native six-bit indices");
	std::array<game::FxEffectDef,17> chain{};
	std::array<game::FxElemDef,17> links{};
	for(unsigned i=0;i<chain.size();++i)
	{
		chain[i].elemDefCountOneShot=1;chain[i].elemDefs=&links[i];
		if(i+1<chain.size())links[i].effectEmitted.handle=&chain[i+1];
	}
	check(!cache.get(&chain[0]) && !cache.size(),"excessive child nesting rejects without retaining a partial graph");
}
