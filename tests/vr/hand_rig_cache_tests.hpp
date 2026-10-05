#pragma once
#include "component/vr/gameplay/hands/rig_cache.hpp"
#include "component/vr/gameplay/optic_view.hpp"

namespace hand_rig_cache_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::hands;
		rig_binding_identity assembly;
		assembly.object=1;assembly.model_array=2;assembly.assets=1;assembly.resource=7;
		assembly.model_count=2;assembly.bone_count=96;assembly.duplicate_parts=9;assembly.admission=15;
		assembly.models[0].model=10;assembly.models[0].bind=11;assembly.models[0].bone_names=12;
		assembly.models[0].parents=13;assembly.models[0].bones=68;assembly.models[0].roots=1;
		assembly.models[0].attachment=255;assembly.models[1].model=20;assembly.models[1].bones=28;
		assembly.models[1].attachment=13;assembly.bones[24].name=100;assembly.bones[24].parent_distance=10;
		assembly.bones[24].bind[3]=0x3f800000u;
		rig_binding_cache cache;
		check(!cache.matches(assembly),"new rig context has no accepted static binding");
		cache.store(assembly,true);
		check(cache.matches(assembly) && cache.accepted(),"unchanged rig is reusable across pose epochs and controller owners");
		const auto changed=[&](auto edit,const char* message)
		{auto next=assembly;edit(next);check(!cache.matches(next),message);};
		changed([](auto& k){++k.object;},"different DObj cannot reuse another context binding");
		changed([](auto& k){++k.resource;},"recycled owned or empty DObj storage invalidates binding by resource generation");
		changed([](auto& k){++k.assets;},"zone unload invalidates even an identical recycled asset address");
		changed([](auto& k){++k.model_array;},"replaced DObj model array invalidates assembly binding");
		changed([](auto& k){++k.models[1].model;},"weapon or attachment model replacement invalidates binding");
		changed([](auto& k){++k.models[0].bind;},"replaced bind buffer invalidates binding with stable XModel");
		changed([](auto& k){++k.models[0].bone_names;},"replaced bone-name table invalidates binding with stable XModel");
		changed([](auto& k){++k.models[0].parents;},"replaced parent table invalidates binding");
		changed([](auto& k){++k.models[1].attachment;},"DObj attachment parent edits invalidate binding");
		changed([](auto& k){++k.duplicate_parts;},"native duplicate aliases participate in assembly identity");
		changed([](auto& k){++k.bone_count;},"bone count changes invalidate binding");
		changed([](auto& k){++k.models[0].roots;},"root topology changes invalidate binding");
		changed([](auto& k){++k.bones[24].name;},"in-place semantic bone edits invalidate binding");
		changed([](auto& k){++k.bones[24].parent_distance;},"in-place bone parent edits invalidate binding");
		changed([](auto& k){++k.bones[24].bind[0];},"in-place rest pose edits invalidate binding");
		changed([](auto& k){++k.models[1].surfaces;},"replaced optic surfaces invalidate bindings");
		changed([](auto& k){++k.models[1].materials;},"replaced optic material table invalidates bindings");
		changed([](auto& k){++k.models[1].bone_info;},"replaced heartbeat bone bounds invalidate bindings");
		changed([](auto& k){++k.models[1].bounds[0];},"in-place optic bounds changes invalidate comfort geometry");
		changed([](auto& k){k.empty=true;},"empty hands and weapon assemblies have distinct admissions");
		changed([](auto& k){k.admission=0;},"shield or visibility readiness changes invalidate prior admission");
		cache.store(assembly,false);
		check(cache.matches(assembly) && !cache.accepted(),"a rejected immutable rig is cached without exposing previous success");
		auto unavailable=assembly;unavailable.admission=0;cache.store(unavailable,false);
		check(!cache.matches(assembly),"visibility recovery retries previously rejected binding");
		auto invalid_bind=assembly;invalid_bind.bones[24].bind[0]=0x7fc00000u;cache.store(invalid_bind,false);
		check(cache.matches(invalid_bind) && !cache.accepted(),"nonfinite bind rejection has a stable exact-bit identity");
		cache.invalidate();check(!cache.matches(invalid_bind) && !cache.accepted(),"unreadable metadata cannot retain stale success or rejection");
		assembly.resource=0;cache.store(assembly,true);
		check(cache.matches(assembly) && cache.accepted(),"native viewmodel without private generation remains cacheable inside its asset epoch");
		++assembly.assets;check(!cache.matches(assembly),"native viewmodel binding expires at asset retirement");
		{
			using vr::gameplay::weapons::optics::reticle_image_state;
			reticle_image_state image{1000,0,64,64,true};
			const auto retained=image.identity();cache.store(assembly,true);
			check(retained && !image.ready(),"reticle identity binds before GPU upload while presentation stays opaque");
			image.shader_view=2000;
			check(image.identity()==retained && image.ready() && cache.matches(assembly),
				"late reticle upload becomes available without rebuilding the cached rig");
			image.shader_view=0;
			check(image.identity()==retained && !image.ready(),"GPU view loss suppresses the lens without discarding its asset identity");
			image.shader_view=3000;
			check(image.identity()==retained && image.ready() && cache.matches(assembly),
				"replacement GPU view recovers through the same immutable binding");
			image.texture_2d=false;check(!image.identity() && !image.ready(),"unsupported reticle image type stays rejected");
			image.texture_2d=true;image.width=3;check(!image.identity() && !image.ready(),"undersized reticle image stays rejected");
		}
	}
}
