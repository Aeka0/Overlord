#include "std_include.hpp"
#include "component/vr/gameplay/first_person_mesh_size.hpp"
#include "component/vr/first_person_size.hpp"
#include "component/vr/gameplay/hands/model_size.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"
#include "component/vr/gameplay/physical_reload_profile.hpp"
#include "component/vr/gameplay/shield_geometry.hpp"
#include "component/vr/gameplay/melee_weapon_profile.hpp"
#include "game/assets.hpp"

namespace mesh = vr::gameplay::first_person_mesh_size;
using namespace vr::gameplay::hands;

int main()
{
	int failures{};
	const auto check = [&](bool value,const char* name)
	{ if (!value) { std::cerr << "FAIL: " << name << '\n'; ++failures; } };
	const auto close = [](vec a,vec b) { return length(sub(a,b)) < .0001f; };
	const std::array<vec,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
	for (const float size : {.2f,.8f,1.f})
	{
		const anchor parent{{100,-40,30},from_axis({{{0,1,0},{-1,0,0},{0,0,1}}}),size};
		const anchor child{{5,3,-2},{0,0,0,1}};
		const auto world=pose_math::compose(parent,child);
		const auto restored=pose_math::compose(pose_math::inverse(parent),world);
		check(close(restored.position,child.position) && std::abs(restored.scale-1.f)<.00001f,
		      "translated/rotated model contacts round-trip at 20, 80 and 100 percent");
		check(close(world.position,transform_point(parent,child.position)),"mesh and contact transforms share the size");
		vr::gameplay::equipment::chest_slots chest{};
		chest.anchors[0]={parent.position,parent.rotation};
		for(unsigned kind=0;kind<unsigned(vr::gameplay::equipment::melee_weapons::kind::count);++kind)
		{
			const auto item=vr::gameplay::equipment::melee_weapons::kind(kind);
			const auto full=vr::gameplay::equipment::melee_weapons::stowed(chest,item);
			const auto sized=vr::gameplay::equipment::melee_weapons::stowed(chest,item,size);
			const auto contact=vr::gameplay::equipment::melee_weapons::get(item).grip_center;
			check(sized.scale==size && close(transform_point(full,contact),transform_point(sized,contact)),
			      "stored melee models retain the same body contact at the selected size");
		}
		vr::gameplay::weapons::reload_profile reload{};
		reload.well.position={5,0,0};reload.magazine_top={1,0,0};
		const auto magazine=pose_math::compose(parent,{{4,0,0},{0,0,0,1}});
		check(close(vr::gameplay::weapons::magazine_tip_in_well(reload,parent,magazine,39.370079f),{}),
		      "the visible scaled magazine tip contacts the actual scaled well");
		vr::gameplay::weapons::physical_reload::box_motion box{};
		box.frame={{1.25f*size,0,0},{0,0,0,1},size};box.region_count=1;box.regions[0].half={1,1,1};
		check(std::abs(vr::gameplay::weapons::physical_reload::closest_box(box,0).distance-.25f*size)<.00001f,
		      "contact volume shrinks with the rendered body");
		const std::array panels{vr::gameplay::weapons::shield::triangle{{0,-2,-2},{0,2,-2},{0,0,2}}};
		vr::gameplay::weapons::shield::profile shield{panels,{}};
		const auto hit=vr::gameplay::weapons::shield::intersect(shield,parent,
		    transform_point(parent,{-1,0,0}),transform_point(parent,{1,0,0}),1,
		    vr::gameplay::weapons::shield::faces::both);
		check(hit.valid && std::abs(hit.fraction-.5f)<.00001f && close(hit.point,parent.position),
		      "shield trace hits its scaled visible panel");
		rig r;r.count=13;r.parent.fill(-1);
		r.arms={arm{1,2,3},arm{5,6,7}};
		r.parent[1]=r.parent[5]=r.parent[9]=0;
		r.parent[2]=1;r.parent[3]=2;r.parent[4]=3;
		r.parent[6]=5;r.parent[7]=6;r.parent[8]=7;
		r.parent[10]=9;r.parent[11]=10;
		r.parent[12]=6;
		r.weapon_tag=9;r.gun=10;r.muzzle=11;r.rear_grip_wrist=7;
		r.weapon_bones[10]=r.weapon_bones[11]=true;r.model_scale=size;
		std::array<bone,13> native{};
		for (auto& b:native) { b.rotation={0,0,0,1};b.weight=2; }
		native[1].position={0,7,-8};native[2].position={10,8,-14};native[3].position={20,5,-10};native[4].position={22,5,-10};
		native[5].position={0,-7,-8};native[6].position={10,-8,-14};native[7].position={20,-5,-10};native[8].position={22,-5,-10};
		native[10].position={19,-5,-10};native[11].position={35,-5,-10};
		const vec forearm{10,3,4},radial{0,1,-.75f};
		native[12].position=add(native[6].position,add(scale(forearm,.735f),radial));
		const auto original=native;
		{
			model_size_scope pending(r,native);
			check(close(native[7].position,original[7].position) &&
			      close(sub(native[8].position,native[7].position),{2*size,0,0}),
			      "tracked joints stay fixed while local finger geometry changes size");
		}
		check(std::memcmp(native.data(),original.data(),sizeof(native))==0,"a rejected size solve restores the native pose");
		size_model_pose(r,native);
		check(close(native[12].position,add(original[6].position,add(scale(forearm,.735f),scale(radial,size)))),
		      "forearm deformation pivots keep their axial placement while sleeve thickness changes");
		const std::array<anchor,2> targets{{{original[3].position,{0,0,0,1},size},{original[7].position,{0,0,0,1},size}}};
		const std::array<vec,2> shoulders{{original[1].position,original[5].position}};
		std::array<bone,13> solved{};std::array<bool,2> limited{};const vec rear{1,0,0};
		check(solve(r,native,targets,shoulders,axes,1,solved,limited,&rear),"scaled native assembly solves");
		check(close(solved[7].position,targets[1].position) && close(solved[3].position,targets[0].position),"both wrists remain on tracking");
		const auto gun=pose_math::as_anchor(solved[10],size);
		check(close(transform_point(gun,rear),targets[1].position),"scaled rear grip stays on the controlling wrist");
		check(close(sub(solved[11].position,targets[1].position),{15*size,0,0}),"muzzle and model length use the same scale");
		check(std::abs(length(sub(solved[6].position,solved[5].position))-
		               length(sub(original[6].position,original[5].position)))<.001f,"arm reach remains anatomical");
	}

	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	const auto created=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
	check(SUCCEEDED(created),"WARP device creates");
	if (FAILED(created)) return 1;
	std::array<game::GfxPackedVertex,3> vertices{};
	for (unsigned i=0;i<vertices.size();++i)
	{
		vertices[i].xyz[0]=float(i+1);vertices[i].xyz[1]=-2;vertices[i].xyz[2]=3;
		vertices[i].normal.packed=0x12345678;vertices[i].texCoord.packed=0x76543210;
	}
	D3D11_BUFFER_DESC descriptor{};
	std::array<game::GfxPackedVertex,4> backing{};
	std::copy(vertices.begin(),vertices.end(),backing.begin()+1);
	for (unsigned structured=0;structured<2;++structured)
	{
		descriptor.ByteWidth=sizeof(backing);descriptor.Usage=D3D11_USAGE_IMMUTABLE;
		descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE|(structured?0:D3D11_BIND_VERTEX_BUFFER);
		descriptor.MiscFlags=structured?D3D11_RESOURCE_MISC_BUFFER_STRUCTURED:D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		descriptor.StructureByteStride=structured?sizeof(game::GfxPackedVertex):0;
		D3D11_SUBRESOURCE_DATA data{backing.data(),0,0};
		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		const auto buffer_result=device->CreateBuffer(&descriptor,&data,&buffer);
		check(SUCCEEDED(buffer_result),"native vertex buffer creates");
		if (FAILED(buffer_result)) return 1;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format=structured?DXGI_FORMAT_UNKNOWN:DXGI_FORMAT_R32_TYPELESS;
		srv.ViewDimension=structured?D3D11_SRV_DIMENSION_BUFFER:D3D11_SRV_DIMENSION_BUFFEREX;
		srv.BufferEx.FirstElement=structured?1:UINT(sizeof(game::GfxPackedVertex)/4);
		srv.BufferEx.NumElements=structured?UINT(vertices.size()):UINT(sizeof(vertices)/4);
		srv.BufferEx.Flags=structured?0:D3D11_BUFFEREX_SRV_FLAG_RAW;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		const auto view_result=device->CreateShaderResourceView(buffer.Get(),&srv,&view);
		check(SUCCEEDED(view_result),"native vertex view creates");
		if (FAILED(view_result)) return 1;
		game::XSurface source{};source.flags=4;source.vertCount=UINT16(vertices.size());
		source.verts0.packedVerts0=vertices.data();source.vb0=buffer.Get();source.vb0View=view.Get();
		std::array<bone,3> bind{};
		for (auto& b:bind) { b.rotation={0,0,0,1};b.position={2,3,4};b.weight=2; }
		for (const float size:{.2f,.8f})
		{
			std::array<std::byte,0x40> packet{};
			const auto* surface=&source;const auto* inverse_bind=bind.data();
			std::memcpy(packet.data()+0x28,&surface,sizeof(surface));
			std::memcpy(packet.data()+8,&inverse_bind,sizeof(inverse_bind));packet[0x11]=std::byte(bind.size());
			const bool prepared=mesh::prepare_packet(packet.data(),size);
			check(prepared,"rigid/skinned packet prepares at requested size");
			if (!prepared) return 1;
			std::memcpy(&surface,packet.data()+0x28,sizeof(surface));
			std::memcpy(&inverse_bind,packet.data()+8,sizeof(inverse_bind));
			check(surface!=&source && inverse_bind!=bind.data() && source.vb0==buffer.Get(),"source assets stay immutable");
			check(close(inverse_bind[0].position,{2*size,3*size,4*size}),"inverse bind scales with vertices");
			auto subset=source;subset.triCount=1;
			auto subset_packet=packet;
			const game::XSurface* subset_source=&subset;const bone* subset_bind=bind.data();
			std::memcpy(subset_packet.data()+0x28,&subset_source,sizeof(subset_source));
			std::memcpy(subset_packet.data()+8,&subset_bind,sizeof(subset_bind));
			check(mesh::prepare_packet(subset_packet.data(),size),"visibility subset prepares");
			std::memcpy(&subset_source,subset_packet.data()+0x28,sizeof(subset_source));
			check(subset_source->vb0==surface->vb0 && subset_source->triCount==1,
			      "ammo/visibility variants reuse a vertex buffer without sharing their triangle selection");
			auto rigid_packet=packet;
			const game::XSurface* rigid_surface=&source;const bone* rigid_bind=bind.data();
			std::memcpy(rigid_packet.data()+0x28,&rigid_surface,sizeof(rigid_surface));
			std::memcpy(rigid_packet.data()+8,&rigid_bind,sizeof(rigid_bind));
			check(mesh::prepare_packet(rigid_packet.data(),size,mesh::packet_data::inverse_bind),"rigid inverse bind prepares before native matrix packing");
			std::memcpy(&rigid_surface,rigid_packet.data()+0x28,sizeof(rigid_surface));
			check(rigid_surface==&source,"native rigid grouping still sees the original source surface");
			check(mesh::prepare_packet(rigid_packet.data(),size,mesh::packet_data::surface),"rigid geometry substitutes after native visibility packing");
			std::memcpy(&rigid_bind,rigid_packet.data()+8,sizeof(rigid_bind));
			check(close(rigid_bind[0].position,{2*size,3*size,4*size}),"separate rigid phases never apply size twice");
			D3D11_BUFFER_DESC readback{};surface->vb0->GetDesc(&readback);
			readback.Usage=D3D11_USAGE_STAGING;readback.BindFlags=0;readback.CPUAccessFlags=D3D11_CPU_ACCESS_READ;readback.MiscFlags=0;readback.StructureByteStride=0;
			Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
			const auto staging_result=device->CreateBuffer(&readback,nullptr,&staging);
			check(SUCCEEDED(staging_result),"vertex readback creates");
			if (FAILED(staging_result)) return 1;
			context->CopyResource(staging.Get(),surface->vb0);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"GPU size copy reads back");
			if (mapped.pData)
			{
				const auto* copied=static_cast<const game::GfxPackedVertex*>(mapped.pData)+1;
				check(close({copied[0].xyz[0],copied[0].xyz[1],copied[0].xyz[2]},{size,-2*size,3*size}) &&
				      copied[0].normal.packed==vertices[0].normal.packed && copied[0].texCoord.packed==vertices[0].texCoord.packed,
				      "GPU positions change size while normals and texture coordinates survive");
				context->Unmap(staging.Get(),0);
			}
		}
	}
	std::array<std::byte,0x200> object{};std::array<bone,1> bones{};
	const void* matrices=bones.data();std::uint32_t epoch=7;
	std::memcpy(object.data()+0xa8,&matrices,sizeof(matrices));std::memcpy(object.data()+0xb0,&epoch,sizeof(epoch));
	mesh::publish(object.data(),matrices,epoch,.2f);
	check(mesh::for_object(object.data())==.2f,"size belongs to the committed native object epoch");
	++epoch;std::memcpy(object.data()+0xb0,&epoch,sizeof(epoch));
	check(mesh::for_object(object.data())==1.f,"a later or foreign native pose does not inherit size");
	mesh::clear_after_drain();
	vr::first_person_size::requested=.8f;
	check(vr::first_person_size::value()==.8f,"status can read size before the first solve");
	vr::first_person_size::requested=.2f;
	check(vr::first_person_size::freeze()==.2f,"a status read cannot freeze an earlier startup value");
	vr::first_person_size::requested=1.f;
	check(vr::first_person_size::value()==.2f,"later reinitialization cannot mix geometry sizes inside one process");
	std::cout << "first-person size: " << failures << " failures\n";
	return failures?1:0;
}
