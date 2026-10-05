#include "component/vr/hand.hpp"
using vr::hand;
#include <std_include.hpp>
#include "game/assets.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"
#include "model_identity_tests.hpp"
#include "console_format_tests.hpp"
#include "animation_index_bridge_tests.hpp"
#include "launcher_aim_bridge_tests.hpp"
#include "use_range_bridge_tests.hpp"
#include "oilrig_angle_bridge_tests.hpp"
#include "startup_callbacks_tests.hpp"
#include "skinned_part_tests.hpp"
#include "opaque_mesh_tests.hpp"
#include "component/vr/gameplay/immutable_surface_cache.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"

int main()
{
	using Microsoft::WRL::ComPtr;
	int failed=model_identity_tests::run()+console_format_tests::run()+animation_index_bridge_tests::run()+launcher_aim_bridge_tests::run()+use_range_bridge_tests::run()+oilrig_angle_bridge_tests::run()+startup_callbacks_tests::run();
	const auto check=[&](bool ok,const char* name) { if (!ok) { ++failed; std::cerr << "FAIL: " << name << '\n'; } };
	{
		struct descriptor{std::uint32_t version{},mask{};std::array<int,4> groups{};const int* borrowed{};};
		vr::gameplay::weapons::immutable_surface_cache<descriptor> cache;
		std::array<int,4> sources{};std::vector<const descriptor*> packets;
		for(auto& source:sources)for(unsigned count=0;count<100;++count)
		{
			const auto* value=cache.get(&source,count,[](const descriptor& d){return d.version==1;},[&](descriptor& d){d.version=1;d.mask=count;d.groups[0]=int(count);d.borrowed=d.groups.data();});
			check(value!=nullptr,"hundreds of belt visibility masks admit beyond the old 64-entry limit");packets.push_back(value);
		}
		for(auto* packet:packets)check(packet && packet->borrowed==packet->groups.data() && packet->borrowed[0]==int(packet->mask),"queued render descriptors retain inline group addresses after hash growth");
		int built{};const auto* again=cache.get(&sources[0],0,[](const descriptor& d){return d.version==1;},[&](descriptor&){++built;});
		check(again==packets[0] && !built && cache.size()==400,"same source and mask reuse immutable descriptor");
		const auto* changed=cache.get(&sources[0],0,[](const descriptor& d){return d.version==2;},[](descriptor& d){d.version=2;});
		check(changed!=again && again->version==1 && cache.size()==401,"changed source metadata retains old in-flight version");
		cache.clear();check(cache.size()==0,"asset drain retires all visibility descriptors");
		vr::gameplay::weapons::immutable_surface_cache<descriptor,sizeof(descriptor)+64> bounded;
		check(bounded.get(&sources[0],0,[](const descriptor&){return true;},[](descriptor&){}) &&
			!bounded.get(&sources[0],1,[](const descriptor&){return true;},[](descriptor&){}),"metadata cache enforces memory budget without evicting borrowed descriptors");
	}
	ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
	if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) return 1;
	failed+=skinned_part_tests::run(device.Get(),context.Get());
	opaque_mesh_tests(device.Get(),context.Get(),check);
	std::array<game::GfxPackedVertex,6> vertices{};
	for (int i=0;i<6;++i) { vertices[i].xyz[0]=float(i); vertices[i].xyz[1]=float(i%3); }
	std::array<game::Face,2> indices{{{0,1,2},{3,4,5}}};
	std::array<game::XRigidVertList,2> groups{{{0,3,0,1,nullptr},{64,3,1,1,nullptr}}};
	std::array<game::DObjAnimMat,2> bind{{{{0,0,0,1},{0,0,0},2},{{0,0,0,1},{4,1,0},2}}};
	std::array<game::scr_string_t,2> names{1,2};
	game::Material material{}; game::Material* material_pointer=&material;
	game::XSurface surface{}; surface.flags=2; surface.vertCount=6; surface.triCount=2;
	 surface.rigidVertListCount=2; surface.rigidVertLists=groups.data(); surface.triIndices=surface.triIndices2=indices.data();
	 surface.verts0.packedVerts0=vertices.data();
	game::XModel source{}; source.numBones=2; source.numRootBones=1; source.numsurfs=1; source.numLods=1;
	source.baseMat=bind.data(); source.boneNames=names.data(); source.materialHandles=&material_pointer;
	source.lodInfo[0].surfs=&surface; source.lodInfo[0].numsurfs=1; source.radius=1; source.scale=1;
	D3D11_BUFFER_DESC vb{}; vb.ByteWidth=sizeof(vertices); vb.Usage=D3D11_USAGE_IMMUTABLE; vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vdata{vertices.data(),0,0}; ComPtr<ID3D11Buffer> vertex_buffer;
	check(SUCCEEDED(device->CreateBuffer(&vb,&vdata,&vertex_buffer)),"source vertex buffer"); surface.vb0=vertex_buffer.Get();
	for (int mode=0;mode<4;++mode)
	{
		D3D11_BUFFER_DESC ib{}; ib.ByteWidth=sizeof(indices); ib.Usage=D3D11_USAGE_IMMUTABLE;
		ib.BindFlags=D3D11_BIND_INDEX_BUFFER | (mode ? D3D11_BIND_SHADER_RESOURCE : 0);
		if (mode==3) ib.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		D3D11_SUBRESOURCE_DATA idata{indices.data(),0,0}; ComPtr<ID3D11Buffer> index_buffer;
		check(SUCCEEDED(device->CreateBuffer(&ib,&idata,&index_buffer)),"source index buffer"); surface.indexBuffer=index_buffer.Get();
		ComPtr<ID3D11ShaderResourceView> srv;
		if (mode)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
			desc.Format=mode==1 ? DXGI_FORMAT_R16_UINT : mode==2 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32_TYPELESS;
			desc.ViewDimension=mode==3 ? D3D11_SRV_DIMENSION_BUFFEREX : D3D11_SRV_DIMENSION_BUFFER;
			desc.Buffer.NumElements=mode==1 ? 6 : 3;
			if (mode==3) desc.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
			check(SUCCEEDED(device->CreateShaderResourceView(index_buffer.Get(),&desc,&srv)),"source index SRV");
		}
		surface.indexBufferView=srv.Get();
		scene_models::rigid_part not_owner;
		check(!not_owner.create(&source,1),"asset construction requires main-owner scope");
		const scheduler::detail::execution_scope main(scheduler::pipeline::main);
		scene_models::rigid_part selected;
		check(selected.create(&source,1),"create independent selected rigid group");
		auto* model=selected.model(); if (!model) continue;
		const auto& mesh=model->lodInfo[0].surfs[0];
		check(mesh.triCount==1 && mesh.triIndices[0].v1==3 && mesh.triIndices[0].v3==5 &&
			mesh.indexBuffer!=surface.indexBuffer && mesh.vb0==surface.vb0,"subset indices are owned, vertex GPU layout reused");
		check(model->bounds.midPoint[0]==4 && model->bounds.halfSize[0]==1 && model->radius>=5,
			"part culling bounds include actual selected geometry, not source model approximate radius");
		check(selected.bind()[0]==4 && selected.bind()[6]==1,"part pivot retains native bind transform");
		{
			using namespace vr::gameplay::hands::pose_math;
			const anchor wanted{{9,7,5},{0,0,.70710678f,.70710678f}};
			const auto binding=selected.bind();
			const anchor original{{binding[0],binding[1],binding[2]},{binding[3],binding[4],binding[5],binding[6]}};
			const auto placement=rigid_delta(wanted,original);
			const auto& p=mesh.verts0.packedVerts0[4]; // Source point (4,1,0) is the selected native pivot.
			check(length(sub(compose(placement,{{p.xyz[0],p.xyz[1],p.xyz[2]},{0,0,0,1}}).position,wanted.position))<.0001f,
				"actual immutable part vertices require inverse bind before rigid scene placement");
		}
		check(selected.source()==&source,"subset carries its exact resource source, not an invented asset identity");
		const auto colored=vr::opaque_mesh::mesh::create(model);
		check(colored && colored->index_count==3,"guide owns compact geometry from the exact rigid subset without any material donor");
		{
			game::Material objective{};scene_models::rigid_part ghost;
			check(ghost.create(&source,1) && ghost.preview_material(&objective),"objective preview uses isolated material handles and vertex buffer");
			if(auto* preview=ghost.model())
			{
				const auto& geometry=preview->lodInfo[0].surfs[0];
				check(preview->materialHandles[0]==&objective && source.materialHandles[0]==&material && geometry.vb0!=surface.vb0 &&
					geometry.verts0.packedVerts0[3].texCoord.packed==0x38003800u && vertices[3].texCoord.packed==0,
					"preview constant sampling removes donor artwork without altering source UVs or native material");
			}
		}
		// Exercise the native admission contract with the actual heap descriptor
		{
			scene_models::rigid_part bent;
			check(bent.create(&source,1) && bent.bake_vertices([](unsigned,std::span<game::GfxPackedVertex> points){for(auto& p:points)p.xyz[2]+=7;return true;}),
				"baked skin geometry receives private immutable vertex buffers");
			if(auto* baked=bent.model())check(baked->lodInfo[0].surfs[0].vb0!=surface.vb0 &&
				baked->lodInfo[0].surfs[0].verts0.packedVerts0[3].xyz[2]==vertices[3].xyz[2]+7 &&
				baked->bounds.midPoint[2]==model->bounds.midPoint[2]+7 && baked->boneInfo->bounds.midPoint[2]==baked->bounds.midPoint[2],
				"baked cable preserves source geometry and updates both model and bone culling bounds");
		}
		// Exercise the native admission contract with the actual heap descriptor
		// emitted by the GPU subset factory, not only synthetic pointer values.
		const scene_models::identity::pool source_pool{reinterpret_cast<std::uintptr_t>(&source),1,sizeof(source)};
		scene_models::identity::registry<1> identities;
		const auto descriptor=reinterpret_cast<std::uintptr_t>(model);
		check(!identities.source(source_pool,descriptor),"new GPU subset has no implicit native pool identity");
		const std::array<scene_models::identity::alias,1> identity{{{descriptor,reinterpret_cast<std::uintptr_t>(selected.source())}}};
		check(identities.publish(source_pool,identity) && identities.source(source_pool,descriptor)==source_pool.first,
			"actual subset registers against its source while retaining independent geometry");
		check(surface.triCount==2 && source.numBones==2 && groups[1].boneOffset==64,"source assets unchanged");
		D3D11_BUFFER_DESC rd{}; mesh.indexBuffer->GetDesc(&rd);
		rd.Usage=D3D11_USAGE_STAGING; rd.BindFlags=0; rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ; rd.MiscFlags=0;
		ComPtr<ID3D11Buffer> staging;
		check(SUCCEEDED(device->CreateBuffer(&rd,nullptr,&staging)),"index readback staging");
		context->CopyResource(staging.Get(),mesh.indexBuffer); D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))
		{
			const auto* values=static_cast<const unsigned short*>(mapped.pData);
			check(values[0]==3 && values[1]==4 && values[2]==5 && values[3]==0,"GPU subset contains correct triangle and zero alignment padding");
			context->Unmap(staging.Get(),0);
		}
		else check(false,"index map");
		if (mode)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; mesh.indexBufferView->GetDesc(&desc);
			check(desc.Buffer.NumElements==(mode==1 ? 4u : 2u),"subset SRV does not retain source element count");
		}
		indices[1].v1=2; scene_models::rigid_part cross_group;
		check(!cross_group.create(&source,1),"cross-bone triangle rejected"); indices[1].v1=3;
		surface.flags=4; scene_models::rigid_part skinned;
		check(!skinned.create(&source,1),"unsupported skinned surface rejected"); surface.flags=2;
		check(!selected.create(&source,0),"published immutable part cannot be overwritten");
		{
			auto rigid=surface;game::XRigidVertList one_group{64,6,0,2,nullptr};
			rigid.rigidVertListCount=1;rigid.rigidVertLists=&one_group;auto prop=source;prop.lodInfo[0].surfs=&rigid;
			const std::array<std::array<unsigned,2>,1> lever{{{1,1}}};
			scene_models::rigid_part spoon,body;
			check(spoon.create_face_partition(&prop,1,lever,true) && body.create_face_partition(&prop,1,lever,false),
				"shared-bone prop supports independent lever and body partitions");
			if(spoon.model() && body.model())check(spoon.model()->lodInfo[0].surfs[0].triCount==1 && body.model()->lodInfo[0].surfs[0].triCount==1 &&
				spoon.model()->lodInfo[0].surfs[0].triIndices[0].v1==3 && body.model()->lodInfo[0].surfs[0].triIndices[0].v1==0 && indices[1].v1==3,
				"lever/body partition preserves all source faces exactly once without source mutation");
			const std::array<std::array<unsigned,2>,2> overlap{{{0,1},{1,1}}};scene_models::rigid_part bad;
			check(!bad.create_face_partition(&prop,1,overlap,true),"overlapping lever face ranges rejected");
			const std::array<std::array<unsigned,2>,1> outside{{{2,2}}};scene_models::rigid_part beyond;
			check(!beyond.create_face_partition(&prop,1,outside,true),"out-of-range lever faces rejected");
		}
		{
			auto skin=surface;skin.flags=6;skin.rigidVertListCount=0;skin.rigidVertLists=nullptr;
			std::array<std::uint16_t,6> weights{0,0,0,64,64,64};
			skin.blendVertCounts[0]=6;skin.blendVerts=weights.data();
			auto prop=source;prop.lodInfo[0].surfs=&skin;
			const std::array<unsigned,1> pin{1};scene_models::rigid_part pin_mesh;
			check(pin_mesh.create_skin_partition(&prop,pin),"skinned prop pin partitions without modifying shared mesh");
			if(auto* result=pin_mesh.model())
			{
				const auto& view=result->lodInfo[0].surfs[0];
				check(view.flags==2 && view.triCount==1 && view.triIndices[0].v1==3 && !view.blendVerts &&
					view.blendVertCounts[0]==0 && view.vb0==skin.vb0 && view.rigidVertListCount==1,
					"static bind-space partition becomes an independent rigid draw with original material vertices");
				check(view.verts0.packedVerts0[3].xyz[0]==vertices[3].xyz[0] && view.verts0.packedVerts0[3].xyz[1]==vertices[3].xyz[1],
					"bind-space prop vertices are not transformed twice");
			}
			check(skin.flags==6 && skin.blendVerts==weights.data() && indices[1].v1==3,"original skinned source stays intact");
			// M4/M82 put a rigid charging control in one skinned surface among
			// unrelated rigid surfaces. Keep its own pivot, not the receiver root.
			auto unrelated=surface;unrelated.rigidVertListCount=1;unrelated.vertCount=3;unrelated.triCount=1;
			game::XRigidVertList body_group{0,3,0,1,nullptr};unrelated.rigidVertLists=&body_group;
			std::array<game::XSurface,2> mixed_surfaces{unrelated,skin};
			std::array<game::Material*,2> mixed_materials{&material,&material};
			auto receiver=prop;receiver.numsurfs=2;receiver.lodInfo[0].numsurfs=2;
			receiver.lodInfo[0].surfs=mixed_surfaces.data();receiver.materialHandles=mixed_materials.data();
			scene_models::rigid_part control;
			check(control.create_rigid_bone(&receiver,1),"pure single-bone control accepts mixed rigid/skinned receiver surfaces");
			if(auto* result=control.model())
				check(result->numsurfs==1 && result->lodInfo[0].surfs[0].triCount==1 &&
					control.bind()[0]==bind[1].trans[0] && control.bind()[1]==bind[1].trans[1] &&
					result->lodInfo[0].surfs[0].triIndices[0].v1==3 && skin.flags==6,
					"control keeps exact selected triangles and bone bind without changing source skinning");
			indices[1].v1=2;scene_models::rigid_part mixed;
			check(!mixed.create_skin_partition(&prop,pin) && !mixed.model(),"body-pin boundary triangles cannot be silently torn apart");indices[1].v1=3;
			indices[1].v1=2;scene_models::rigid_part crossing;
			check(!crossing.create_rigid_bone(&receiver,1),"single-bone control rejects a triangle crossing into the receiver");indices[1].v1=3;
			std::array<std::uint16_t,8> blended{0,0,0,64,64,64,0,32768};
			mixed_surfaces[1].blendVertCounts[0]=5;mixed_surfaces[1].blendVertCounts[1]=1;mixed_surfaces[1].blendVerts=blended.data();
			scene_models::rigid_part deforming;
			check(!deforming.create_rigid_bone(&receiver,1),"multi-bone deformation is never approximated as a rigid guide");
			weights[5]=65;scene_models::rigid_part malformed;
			check(!malformed.create_skin_partition(&prop,pin),"invalid skin bone offsets rejected");
		}
		// AK-style magazine body + round groups occupy one of nine receiver
		// surfaces. Remaining surfaces belong to another bone and must stay out.
		auto large=source;
		std::array<game::DObjAnimMat,3> large_bind{bind[0],bind[1],bind[0]};
		std::array<game::scr_string_t,3> large_names{1,2,3};
		std::array<game::XSurface,9> surfaces;
		std::array<game::Material*,9> materials; materials.fill(&material);
		game::XRigidVertList other{128,3,0,1,nullptr};
		for (auto& s:surfaces) { s=surface; s.vertCount=3; s.triCount=1; s.rigidVertListCount=1; s.rigidVertLists=&other; }
		surfaces.back()=surface;
		large.numBones=3; large.numsurfs=9; large.baseMat=large_bind.data(); large.boneNames=large_names.data();
		large.materialHandles=materials.data(); large.lodInfo[0].surfs=surfaces.data(); large.lodInfo[0].numsurfs=9;
		const std::array<unsigned,2> body_round{0,1};
		scene_models::rigid_part combined,body_only,missing,duplicate,foreign_anchor,cross_surface;
		check(body_only.create(&large,1),"single group can be selected from a nine-surface receiver");
		check(combined.create(&large,1,body_round),"body and round groups compose around one magazine pivot");
		if (combined.model())
		{
			const auto& combined_surface=combined.model()->lodInfo[0].surfs[0];
			check(combined_surface.triCount==2 && combined_surface.triIndices[0].v1==0 && combined_surface.triIndices[1].v1==3 &&
				combined_surface.vb0==surface.vb0 && combined.bind()==selected.bind(),"combined subset preserves surface-global indices, vertices and selected pivot");
		}
		const std::array<unsigned,2> repeated{1,1},separate{1,2};
		{
			// Counted magazine subsets filter faces across body/round groups,
			// including level zero with no faces from the declared round group.
			const std::array<scene_models::surface_face_range,1> empty{{{8,0,0}}},loaded{{{8,0,1}}},foreign{{{0,0,0}}};
			scene_models::rigid_part zero,one,wrong_group;
			check(zero.create_face_partition(&large,0,body_round,empty) && one.create_face_partition(&large,0,body_round,loaded),
				"counted body and loaded view prepare independently across multiple source bones");
			if(zero.model() && one.model())
			{
				const auto& a=zero.model()->lodInfo[0].surfs[0];const auto& b=one.model()->lodInfo[0].surfs[0];
				check(a.triCount==1 && b.triCount==2 && a.triIndices[0].v1==0 && b.triIndices[1].v1==3 &&
					a.vb0==surface.vb0 && b.vb0==surface.vb0 && a.indexBuffer!=b.indexBuffer,
					"population variants own immutable indices and share original vertex buffers");
				check(zero.bind()==one.bind() && zero.model()->materialHandles[0]==&material && source.numBones==2,
					"population changes preserve magazine pivot, material and original source");
			}
			check(!wrong_group.create_face_partition(&large,0,body_round,foreign) && !wrong_group.model(),
				"face range from another bone cannot publish a partial counted magazine");
		}
		// Live M240 has 33 surfaces: model admission must not share the old
		// 32-entry rigid-group limit. Also exercise the native byte-count edge.
		for(unsigned count:{33u,255u})
		{
			auto receiver=large;
			std::vector<game::XSurface> many(count,surfaces.front());
			std::vector<game::Material*> many_materials(count,&material);
			many.back()=surface;
			receiver.numsurfs=static_cast<unsigned char>(count);receiver.lodInfo[0].numsurfs=static_cast<unsigned short>(count);
			receiver.lodInfo[0].surfs=many.data();receiver.materialHandles=many_materials.data();
			scene_models::rigid_part box,all_materials;
			check(box.create(&receiver,1) && box.model()->numsurfs==1,"box in final surface survives 33/255-surface receiver admission");
			check(all_materials.create(&receiver,1,separate) && all_materials.model()->numsurfs==count,
				"all selected surfaces fit stable descriptor storage through native maximum");
			check(receiver.numsurfs==count && many.back().triCount==2,"large receiver source geometry stays immutable");
			--receiver.lodInfo[0].numsurfs;scene_models::rigid_part malformed;
			check(!malformed.create(&receiver,1),"larger capacity still rejects mismatched LOD surface count");
		}
		const std::array<unsigned,1> first{0};
		check(!duplicate.create(&large,1,repeated),"duplicate selected group rejected");
		check(!foreign_anchor.create(&large,1,first),"anchor must belong to selected part");
		game::Material second_material{}; materials.back()=&second_material;
		check(cross_surface.create(&large,1,separate),"all selected material surfaces compose without dropping geometry");
		if (auto* all=cross_surface.model())
		{
			check(all->numsurfs==9 && all->materialHandles[0]==&material && all->materialHandles[8]==&second_material,
				"independent subset retains each native material in surface order");
			check(all->lodInfo[0].surfs[8].triIndices[0].v1==3 && all->bounds.halfSize[0]==2.5f,
				"multi-material indices and aggregate bounds include body and rounds");
		}
		// MP5 has a skinned strap between the separate rigid body and bullet.
		surfaces[0].flags=6; surfaces[0].partBits[0]=0x20000000;
		scene_models::rigid_part unrelated_skin,selected_skin,unproven_skin,atomic_failure;
		check(unrelated_skin.create(&large,0,body_round),"native mask proves unrelated skinned strap can be excluded");
		check(!selected_skin.create(&large,1,separate),"selected skinned geometry rejects the whole subset");
		surfaces[0].partBits[0]=0;
		check(!unproven_skin.create(&large,0,body_round),"zero skinned mask cannot prove safe exclusion");
		surfaces[0].flags=2; materials.back()=nullptr;
		check(!atomic_failure.create(&large,1,separate) && !atomic_failure.model(),"late missing material cannot publish partial output");
		materials.back()=&material;
		surfaces.back().rigidVertListCount=1; surfaces.back().triCount=1; surfaces.back().vertCount=3;
		check(!missing.create(&large,0,body_round),"missing round group cannot silently create only body");
	}
	std::cout << "Rigid part WARP tests: " << (failed ? "FAIL" : "PASS") << '\n';
	return failed ? 1 : 0;
}
