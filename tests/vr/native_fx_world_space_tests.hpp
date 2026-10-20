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
	root.name="vfx/shelleject/pistol_view";child.name="vfx/shelleject/pistol_resting";
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
	{
		std::array<game::FxElemVisStateSample, 2> samples{};
		for (auto& sample : samples)
		{
			sample.base.scale = .25f;
			sample.amplitude.scale = .1f;
			sample.base.color[3] = .75f;
		}
		std::array<game::FxElemVisuals, 2> models{};
		models[0].model = models[1].model = &model;
		auto replacement_elements = elements;
		auto replacement_child = child_element;
		auto replacement_root = root, child_fx = child;
		replacement_root.elemDefs = replacement_elements.data();
		child_fx.elemDefs = &replacement_child;
		replacement_child.visualCount = 2;
		replacement_child.visuals.array = models.data();
		replacement_child.visSamples = samples.data();
		replacement_child.visStateIntervalCount = 1;
		replacement_elements[0].visSamples = samples.data();
		replacement_elements[0].visStateIntervalCount = 1;
		replacement_elements[0].flags &= ~game::FX_ELEM_USE_MODEL_PHYSICS;
		replacement_elements[0].effectOnImpact.handle = replacement_elements[0].effectOnDeath.handle =
		    replacement_elements[0].effectEmitted.handle = &child_fx;
		replacement_elements[0].flags |= game::FX_ELEM_SPAWN_IMPACT_FX_WITH_SURFACE_NAME;
		auto surface_fx = child_fx;
		surface_fx.name = "vfx/shelleject/rifle_resting_metal";
		static game::FxEffectDef* material_effect{};
		material_effect = &surface_fx;
		const auto material_lookup = +[](const game::FxEffectDef*, unsigned surface) -> game::FxEffectDef* {
			return surface == 17 ? material_effect : nullptr;
		};
		auto replacement_runners = runners;
		replacement_runners[0].effectDef.handle = &child_fx;
		replacement_runners[1].effectDef.handle = &replacement_root;
		replacement_elements[1].visuals.array = replacement_runners.data();
		replacement_elements[2].visuals.instance.effectDef.handle = &child_fx;
		game::XModel cheytac_case{}, barrett_case{};
		cheytac_case.bounds.halfSize[1] = 1.56334f;
		barrett_case.bounds.halfSize[1] = 2.09963f;
		barrett_case.radius = 2.2f;
		world_space_cache variants;
		auto* cheytac_fx = variants.get(&replacement_root, {1, &cheytac_case}, material_lookup);
		auto* large = variants.get(&replacement_root, {3, &barrett_case}, material_lookup);
		auto* ordinary = variants.get(&replacement_root, {}, material_lookup);
		check(cheytac_fx && large && ordinary && cheytac_fx != large && large != ordinary &&
		          variants.size() == 9,
		      "one native rifle graph supports independent M200, Barrett and unchanged generic shells");
		if (cheytac_fx && large)
		{
			const auto warmed = variants.size();
			auto* metal = variants.get(&surface_fx, {3, &barrett_case});
			check(
			    metal && variants.size() == warmed &&
			        metal->elemDefs[0].visuals.array[0].model == &barrett_case,
			    "surface-table effects are prepared before collision workers select their material variant");
			const auto& altered = large->elemDefs[0];
			const auto& resting = altered.effectOnImpact.handle->elemDefs[0];
			check(
			    altered.visuals.instance.model == &barrett_case && resting.visuals.array != models.data() &&
			        resting.visuals.array[0].model == &barrett_case &&
			        resting.visuals.array[1].model == &barrett_case &&
			        altered.collBounds.halfSize[1] == barrett_case.bounds.halfSize[1] &&
			        resting.collBounds.halfSize[1] == barrett_case.bounds.halfSize[1],
			    "all flight, impact, death and runner models keep the weapon-specific case and collision size");
			check(
			    altered.visSamples != samples.data() && altered.visSamples[0].base.scale == 1 &&
			        altered.visSamples[1].amplitude.scale == 0 &&
			        altered.visSamples[0].base.color[3] == .75f && altered.velSamples == &velocity &&
			        samples[0].base.scale == .25f && models[0].model == &model,
			    "native size curves cannot shrink custom cases; opacity, velocity and shared assets stay intact");
			using namespace vr::gameplay::native_fx::checkpoint;
			const auto id = parse(large->name);
			world_space_cache restored;
			game::XModel fresh = barrett_case;
			auto* recovered =
			    id ? restored.get(&replacement_root, {id->attached, &fresh}, material_lookup) : nullptr;
			check(id && id->kind == variant::casing && id->attached == 3 && recovered &&
			          std::string_view(recovered->name) == large->name &&
			          recovered->elemDefs[0].effectOnDeath.handle->elemDefs[0].visuals.array[0].model ==
			              &fresh,
			      "checkpoint recreation rebinds the complete case graph to fresh model storage");
			registry dictionary;
			const auto definitions = variants.checkpoint_definitions();
			check(dictionary.publish({definitions.data(), variants.size()}),
			      "case graph identities publish with generic FX");
			dictionary.remove(variant::casing);
			check(dictionary.size() == 3, "case asset retirement preserves the shared generic FX dictionary");
		}
		world_space_cache rejected;
		check(!rejected.get(&replacement_root, {1, nullptr}) &&
		          !rejected.get(&replacement_root, {0, &barrett_case}),
		      "partial case replacement identity never emits a native or mismatched model");
		replacement_elements[0].flags |= game::FX_ELEM_USE_MODEL_PHYSICS;
		check(!rejected.get(&replacement_root, {3, &barrett_case}) && !rejected.size(),
		      "unreviewed XModel rigid-body physics rejects before publishing a partial case graph");
	}
	{
		using namespace vr::gameplay::native_fx::checkpoint;
		registry dictionary;
		const auto private_definitions=cache.checkpoint_definitions();
		check(dictionary.publish({private_definitions.data(),cache.size()}),"all private roots and children enter checkpoint dictionary before emission");
		std::array<game::FxEffectDef*,native_capacity> native{};native[0]=&root;native[1]=&child;
		std::size_t count=2;
		check(dictionary.append(native,count) && count==4,"native checkpoint dictionary includes private definitions alongside native assets");
		// A different process/asset allocation resolves the saved old pointer by
		// its identity, then reconstructs the exact presentation and child graph.
		auto next_root=root,next_child=child;
		auto next_elements=elements;
		next_root.elemDefs=next_elements.data();
		next_elements[0].effectOnImpact.handle=next_elements[0].effectOnDeath.handle=next_elements[0].effectEmitted.handle=&next_child;
		auto next_runners=runners;next_runners[0].effectDef.handle=&next_child;next_runners[1].effectDef.handle=&next_root;
		next_elements[1].visuals.array=next_runners.data();next_elements[2].visuals.instance.effectDef.handle=&next_child;
		world_space_cache restored;
		const auto id=parse(world->name);
		auto* replacement=id && id->source==next_root.name?restored.get(&next_root):nullptr;
		check(replacement && replacement!=world && replacement!=&next_root &&
			std::string_view(replacement->name)==world->name &&
			!(replacement->elemDefs[0].flags&game::FX_ELEM_DRAW_WITH_VIEWMODEL) &&
			replacement->elemDefs[0].effectOnDeath.handle!=world_child,
			"checkpoint old-pointer mapping restores world FX with fresh addresses and unchanged depth policy");
		const auto linked=parse(world_child->name);
		check(linked && linked->source==child.name && replacement &&
			std::string_view(replacement->elemDefs[0].effectOnDeath.handle->name)==world_child->name,
			"saved child definitions retain independent stable identities");
	}
	std::array<game::FxEffectDef, world_space_cache::capacity - 1> more{};
	std::array<std::string, world_space_cache::capacity - 1> more_names;
	for(unsigned i=0;i<more.size();++i){more_names[i]="fx/more/"+std::to_string(i);more[i].name=more_names[i].c_str();}
	for (unsigned i = 0; i < more.size() - 1; ++i)
		check(cache.get(&more[i]) != nullptr, "bounded shell cache admission");
	check(!cache.get(&more.back()) && cache.get(&root)==world && world->elemDefs[0].visuals.instance.model==&model,
		"capacity exhaustion never evicts in-flight FX descriptors");
	cache.clear();check(!cache.size(),"native asset drain retires shell descriptors");
	child.elemDefCountOneShot=-1;
	check(!cache.get(&root) && !cache.size(),"invalid child rejects complete graph without partial publication");
	child=original_child;
	check(cache.get(&root) && cache.size()==2,"failed graph construction does not poison the cache");
	cache.clear();
	game::FxEffectDef invalid{};invalid.name="fx/invalid";invalid.elemDefCountOneShot=1;
	check(!cache.get(&invalid),"missing element storage rejects shell FX");
	invalid.elemDefs=elements.data();invalid.elemDefCountOneShot=std::numeric_limits<int>::max();
	check(!cache.get(&invalid),"oversized counts reject before arithmetic or allocation");
	invalid.elemDefCountOneShot=33;invalid.elemDefCountLooping=32;
	check(!cache.get(&invalid),"combined element count respects native six-bit indices");
	std::array<game::FxEffectDef,17> chain{};
	std::array<game::FxElemDef,17> links{};
	for(unsigned i=0;i<chain.size();++i)
	{
		chain[i].name="fx/chain";
		chain[i].elemDefCountOneShot=1;chain[i].elemDefs=&links[i];
		if(i+1<chain.size())links[i].effectEmitted.handle=&chain[i+1];
	}
	check(!cache.get(&chain[0]) && !cache.size(),"excessive child nesting rejects without retaining a partial graph");
}
