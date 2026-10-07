#include <std_include.hpp>
#include "scene_rigid_part.hpp"
#include "scene_surface_indices.hpp"
#include "scheduler_context.hpp"
#include <utils/native_memory.hpp>
#include "vr/gameplay/skinned_part_visibility.hpp"
#include "game/assets.hpp"
#include <wrl/client.h>
#include <cfloat>
#include <cmath>

namespace scene_models
{
	using Microsoft::WRL::ComPtr;
	struct rigid_part::storage
	{
		game::XModel* source{};
		game::XModel model{};
		game::XModelSurfs lod{};
		// Sized once from XModel's byte-sized surface count. Never resize after
		// publishing native pointers; receivers such as M240 have 33 surfaces.
		std::vector<game::XSurface> surfaces;
		std::vector<game::Material*> materials;
		struct mesh_storage
		{
			game::XRigidVertList group{};
			std::vector<game::Face> indices;
			std::vector<game::GfxPackedVertex> vertices;
			ComPtr<ID3D11Buffer> vertex_buffer,index_buffer;
			ComPtr<ID3D11ShaderResourceView> vertex_view,index_view;
		};
		std::vector<mesh_storage> meshes;
		explicit storage(unsigned capacity):surfaces(capacity),materials(capacity),meshes(capacity){}
		unsigned count{};
		game::scr_string_t name{};
		game::DObjAnimMat identity{{0,0,0,1},{0,0,0},2};
		game::XBoneInfo bone_info{};
		std::array<float,7> bind{};
	};
	rigid_part::rigid_part() = default;
	rigid_part::~rigid_part() = default;

	bool rigid_part::create_instances(std::span<const instance> instances)
	{
		status_ = "rigid instances owner/source contract rejected";
		if (data_ || !scheduler::is_executing(scheduler::pipeline::main) ||
		    instances.empty() || instances.size() > 5 || !instances[0].geometry ||
		    !instances[0].geometry->data_)
			return false;

		const auto& first = *instances[0].geometry->data_;
		unsigned surface_capacity{};
		for (const auto& instance : instances)
		{
			if (!instance.geometry || !instance.geometry->data_ ||
			    instance.geometry->source() != first.source)
				return false;
			for (const auto value : instance.translation)
				if (!std::isfinite(value) || std::abs(value) > 10000)
					return false;
			surface_capacity += instance.geometry->data_->count;
		}
		if (!surface_capacity || surface_capacity > 255)
			return false;

		auto out = std::make_unique<storage>(surface_capacity);
		out->source = first.source;
		out->bind = first.bind;
		unsigned copied_vertices{}, copied_faces{};
		for (const auto& instance : instances)
		{
			const auto& source = *instance.geometry->data_;
			for (unsigned surface_index = 0; surface_index < source.count; ++surface_index)
			{
				const auto& original = source.surfaces[surface_index];
				const auto& input = source.meshes[surface_index];
				// These parts have already passed the rigid-face/weight contract.
				// Merge only identical native material/vertex-layout identities.
				unsigned destination{};
				for (; destination < out->count; ++destination)
					if (out->materials[destination] == source.materials[surface_index] &&
					    out->surfaces[destination].flags == original.flags &&
					    out->meshes[destination].vertex_buffer == input.vertex_buffer)
						break;
				if (destination == out->count)
				{
					out->surfaces[destination] = original;
					out->materials[destination] = source.materials[surface_index];
					out->meshes[destination].vertex_buffer = input.vertex_buffer;
					out->meshes[destination].vertex_view = input.vertex_view;
					++out->count;
				}

				auto& output = out->meshes[destination];
				if (output.indices.size() + input.indices.size() > UINT16_MAX)
					return false;
				std::vector<unsigned> remap(input.vertices.size(), UINT_MAX);
				for (const auto& triangle : input.indices)
				{
					if (++copied_faces > 262144) return false;
					game::Face copied{};
					const unsigned indices[]{triangle.v1, triangle.v2, triangle.v3};
					unsigned short* corners[]{&copied.v1, &copied.v2, &copied.v3};
					for (unsigned corner = 0; corner < 3; ++corner)
					{
						const auto index = indices[corner];
						if (index >= input.vertices.size())
							return false;
						if (remap[index] == UINT_MAX)
						{
							if (output.vertices.size() >= UINT16_MAX || ++copied_vertices > 262144)
								return false;
							auto vertex = input.vertices[index];
							for (unsigned axis = 0; axis < 3; ++axis)
								vertex.xyz[axis] += instance.translation[axis];
							remap[index] = static_cast<unsigned>(output.vertices.size());
							output.vertices.push_back(vertex);
						}
						*corners[corner] = static_cast<unsigned short>(remap[index]);
					}
					output.indices.push_back(copied);
				}
			}
		}

		for (unsigned index = 0; index < out->count; ++index)
		{
			auto& surface = out->surfaces[index];
			auto& mesh = out->meshes[index];
			ComPtr<ID3D11Device> device;
			mesh.vertex_buffer->GetDevice(&device);
			if (!device || !create_surface_indices(device.Get(), surface.indexBufferView,
			        std::as_bytes(std::span(mesh.indices)), mesh.index_buffer, mesh.index_view))
				return false;

			surface.vertCount = static_cast<unsigned short>(mesh.vertices.size());
			surface.triCount = static_cast<unsigned short>(mesh.indices.size());
			surface.verts0.packedVerts0 = mesh.vertices.data();
			surface.triIndices = surface.triIndices2 = mesh.indices.data();
			surface.indexBuffer = mesh.index_buffer.Get();
			surface.indexBufferView = mesh.index_view.Get();
			mesh.group = {0, surface.vertCount, 0, surface.triCount, nullptr};
			surface.rigidVertLists = &mesh.group;
			surface.rigidVertListCount = 1;
		}

		// Rebind every pointer copied from the first validated model. Bounds and
		// private vertex buffers are calculated by the existing baking path.
		out->model = first.model;
		out->lod = first.lod;
		out->model.numsurfs = static_cast<unsigned char>(out->count);
		out->model.boneNames = &out->name;
		out->model.baseMat = &out->identity;
		out->model.boneInfo = &out->bone_info;
		out->model.materialHandles = out->materials.data();
		out->lod.surfs = out->surfaces.data();
		out->lod.numsurfs = static_cast<unsigned short>(out->count);
		out->model.lodInfo[0].numsurfs = out->model.numsurfs;
		out->model.lodInfo[0].surfs = out->surfaces.data();
		out->model.lodInfo[0].modelSurfs = &out->lod;
		data_ = std::move(out);
		if (!bake_vertices([](unsigned, std::span<game::GfxPackedVertex>) { return true; }))
		{
			data_.reset();
			status_ = "rigid instance bounds or vertex upload rejected";
			return false;
		}
		status_ = "ready";
		return true;
	}
	game::XModel* rigid_part::model() const noexcept { return data_ ? &data_->model : nullptr; }
	game::XModel* rigid_part::source() const noexcept { return data_ ? data_->source : nullptr; }
	std::array<float,7> rigid_part::bind() const noexcept { return data_ ? data_->bind : std::array<float,7>{}; }
	bool rigid_part::preview_material(game::Material* material)
	{
		if(!data_ || !material || !scheduler::is_executing(scheduler::pipeline::main))return false;
		for(unsigned i=0;i<data_->count;++i){for(auto& v:data_->meshes[i].vertices)v.texCoord.packed=0x38003800u;data_->materials[i]=material;}
		return upload_vertices();
	}
	bool rigid_part::bake_vertices(const std::function<bool(unsigned,std::span<game::GfxPackedVertex>)>& transform)
	{
		if(!data_ || !transform || !scheduler::is_executing(scheduler::pipeline::main))return false;
		float low[3]{FLT_MAX,FLT_MAX,FLT_MAX},high[3]{-FLT_MAX,-FLT_MAX,-FLT_MAX},radius{};
		for(unsigned i=0;i<data_->count;++i)
		{
			auto& mesh=data_->meshes[i];if(!transform(i,mesh.vertices))return false;
			for(const auto& face:mesh.indices)for(auto index:{face.v1,face.v2,face.v3})
			{
				float squared{};for(unsigned c=0;c<3;++c){const float x=mesh.vertices[index].xyz[c];if(!std::isfinite(x) || std::abs(x)>10000)return false;
					low[c]=std::min(low[c],x);high[c]=std::max(high[c],x);squared+=x*x;}radius=std::max(radius,squared);
			}
		}
		for(unsigned c=0;c<3;++c){data_->model.bounds.midPoint[c]=(low[c]+high[c])*.5f;data_->model.bounds.halfSize[c]=(high[c]-low[c])*.5f;}
		data_->model.radius=std::sqrt(radius);
		data_->bone_info.bounds=data_->model.bounds;data_->bone_info.radiusSquared=radius;
		return upload_vertices();
	}
	bool rigid_part::upload_vertices()
	{
		for(unsigned i=0;i<data_->count;++i)
		{
			auto& mesh=data_->meshes[i];auto& surface=data_->surfaces[i];
			D3D11_BUFFER_DESC desc{};mesh.vertex_buffer->GetDesc(&desc);
			desc.ByteWidth=static_cast<UINT>(mesh.vertices.size()*sizeof(game::GfxPackedVertex));desc.Usage=D3D11_USAGE_IMMUTABLE;desc.CPUAccessFlags=0;
			ComPtr<ID3D11Device> device;mesh.vertex_buffer->GetDevice(&device);
			D3D11_SUBRESOURCE_DATA init{mesh.vertices.data(),0,0};ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> view;
			if(FAILED(device->CreateBuffer(&desc,&init,&buffer)))return false;
			if(mesh.vertex_view)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};mesh.vertex_view->GetDesc(&srv);
				unsigned stride=(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)?desc.StructureByteStride:0;
				if(!stride)
				{
					switch(srv.Format)
					{
					case DXGI_FORMAT_R32_UINT:case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:stride=4;break;
					case DXGI_FORMAT_R32G32_UINT:case DXGI_FORMAT_R32G32_FLOAT:stride=8;break;
					case DXGI_FORMAT_R32G32B32A32_UINT:case DXGI_FORMAT_R32G32B32A32_FLOAT:stride=16;break;
					default:return false;
					}
				}
				if(srv.ViewDimension!=D3D11_SRV_DIMENSION_BUFFER && srv.ViewDimension!=D3D11_SRV_DIMENSION_BUFFEREX)return false;
				srv.Buffer.FirstElement=0;srv.Buffer.NumElements=desc.ByteWidth/stride;
				if(FAILED(device->CreateShaderResourceView(buffer.Get(),&srv,&view)))return false;
			}
			mesh.vertex_buffer=std::move(buffer);mesh.vertex_view=std::move(view);surface.vb0=mesh.vertex_buffer.Get();surface.vb0View=mesh.vertex_view.Get();
		}
		return true;
	}
	namespace
	{
		template<class T> bool read(const T* source,T& value) noexcept
		{ return utils::native_memory::read_bytes(&value,source,sizeof(value)); }
	}
	bool rigid_part::create(game::XModel* source,unsigned bone)
	{ return create(source,bone,{&bone,1}); }
	bool rigid_part::create_rigid_bone(game::XModel* source,unsigned bone)
	{return create_impl(source,bone,{&bone,1},membership::rigid_bone);}
	bool rigid_part::create(game::XModel* source,unsigned bone,std::span<const unsigned> bones)
	{return create_impl(source,bone,bones,membership::rigid);}
	bool rigid_part::create_skin_partition(game::XModel* source,std::span<const unsigned> bones)
	{return create_impl(source,0,bones,membership::static_skin);}
	bool rigid_part::create_face_partition(game::XModel* source,unsigned bone,std::span<const std::array<unsigned,2>> ranges,bool include)
	{
		game::XModel model;if(!read(source,model) || model.numsurfs!=1 || ranges.empty() || ranges.size()>64)return false;
		std::vector<surface_face_range> converted;converted.reserve(ranges.size());
		for(const auto& r:ranges)converted.push_back({0,r[0],r[1]});
		return create_impl(source,bone,{&bone,1},membership::rigid,converted,include);
	}
	bool rigid_part::create_face_partition(game::XModel* source,unsigned bone,std::span<const surface_face_range> ranges,bool include)
	{return !ranges.empty() && create_impl(source,bone,{&bone,1},membership::rigid,ranges,include);}
	bool rigid_part::create_face_partition(game::XModel* source,unsigned anchor,std::span<const unsigned> bones,std::span<const surface_face_range> ranges)
	{return !ranges.empty() && create_impl(source,anchor,bones,membership::rigid,ranges,true);}
	bool rigid_part::create_static_face_partition(game::XModel* source, unsigned anchor,
		std::span<const unsigned> bones, std::span<const surface_face_range> ranges)
	{
		return !ranges.empty() && create_impl(source, anchor, bones, membership::rigid_bone, ranges, true);
	}
	bool rigid_part::create_impl(game::XModel* source,unsigned bone,std::span<const unsigned> bones,membership mode,
		std::span<const surface_face_range> face_ranges,bool include)
	{
		const bool skin_partition=mode==membership::static_skin;
		status_="rigid part owner/immutable state rejected";
		if (data_ || !scheduler::is_executing(scheduler::pipeline::main)) return false;
		status_="rigid part source model/LOD contract rejected";
		game::XModel model;
		if (!read(source,model) || bone>=model.numBones || model.numLods!=1 || !model.numsurfs ||
			!model.baseMat || !model.boneNames || model.lodInfo[0].numsurfs!=model.numsurfs ||
			model.lodInfo[0].surfIndex!=0 || !model.lodInfo[0].surfs || !model.materialHandles) return false;
		if (bones.empty() || bones.size()>16 || (!skin_partition && std::find(bones.begin(),bones.end(),bone)==bones.end())) return false;
		unsigned expected_faces{},matched_faces{};
		if(!face_ranges.empty())
		{
			if(skin_partition || face_ranges.size()>256)return false;
			std::array<unsigned,255> counts{};
			for(unsigned n=0;n<model.numsurfs;++n)
			{
				game::XSurface surface;if(!read(model.lodInfo[0].surfs+n,surface))return false;counts[n]=surface.triCount;
			}
			if(!valid_face_partition(face_ranges,std::span(counts).first(model.numsurfs)))return false;
			for(const auto& range:face_ranges)expected_faces+=range.last-range.first+1;
		}
		for (size_t i=0;i<bones.size();++i)
			if (bones[i]>=model.numBones || std::find(bones.begin(),bones.begin()+i,bones[i])!=bones.begin()+i) return false;
		game::DObjAnimMat bind;
		status_="rigid part bind transform rejected";
		if (!read(model.baseMat+bone,bind)) return false;
		float norm{};
		for (auto f : bind.quat) { if (!std::isfinite(f)) return false; norm+=f*f; }
		if (std::abs(norm-1)> .01f) return false;
		for (auto f : bind.trans) if (!std::isfinite(f) || std::abs(f)>10000) return false;
		auto out=std::make_unique<storage>(model.numsurfs);
		out->source=source;
		status_="rigid group or surface-global index contract rejected";
		unsigned seen{};
		for (unsigned n=0;n<model.numsurfs;++n)
		{
			game::XSurface surface;
			if (!read(model.lodInfo[0].surfs+n,surface)) return false;
			if(skin_partition || (mode==membership::rigid_bone && (surface.flags&4u)))
			{
				status_="skin partition requires complete bind-space triangles";
				if((surface.flags&~7u) || !(surface.flags&4u) || !surface.vertCount || !surface.triCount || surface.subdiv ||
					surface.blendShapesCount || !surface.blendVerts || !surface.triIndices || !surface.vb0 || !surface.indexBuffer)return false;
				std::array<bool,256> excluded{};excluded.fill(true);for(auto b:bones)excluded[b]=false;
				std::size_t words{},vertices{};
				for(unsigned i=0;i<8;++i){if(surface.blendVertCounts[i]<0)return false;vertices+=surface.blendVertCounts[i];words+=surface.blendVertCounts[i]*(2*i+1);}
				if(vertices!=surface.vertCount || words>65535*15)return false;
				std::vector<std::uint16_t> blend(words),triangles(std::size_t(surface.triCount)*3);
				if(!utils::native_memory::read_bytes(blend.data(),surface.blendVerts,blend.size()*2) ||
					!utils::native_memory::read_bytes(triangles.data(),surface.triIndices,triangles.size()*2))return false;
				auto partition=vr::gameplay::weapons::partition_skin(surface.blendVertCounts,blend,triangles,surface.vertCount,model.numBones,excluded);
				if(!partition.valid)return false;
				if (!face_ranges.empty())
				{
					std::vector<std::uint16_t> filtered;
					size_t retained_index{};
					for (unsigned face = 0; face < surface.triCount; ++face)
					{
						const auto original = triangles.begin() + face * 3;
						const bool owned = retained_index + 3 <= partition.retained.size() &&
							std::equal(original, original + 3, partition.retained.begin() + retained_index);
						const bool requested = face_in_partition(face_ranges, n, face);
						if (requested)
						{
							if (!owned) return false;
							++matched_faces;
						}
						if (owned)
						{
							if (requested == include) filtered.insert(filtered.end(), original, original + 3);
							retained_index += 3;
						}
					}
					partition.retained = std::move(filtered);
				}
				for(std::size_t at=0,extra=0;extra<8;++extra)for(int v=0;v<surface.blendVertCounts[extra];++v)
				{
					for(std::size_t k=0;k<=extra;++k){const auto b=blend[at+(k?2*k-1:0)]/64;
						const auto found=std::find(bones.begin(),bones.end(),b);if(found!=bones.end())seen|=1u<<unsigned(found-bones.begin());}
					at+=2*extra+1;
				}
				if(partition.retained.empty())continue;
				auto& mesh=out->meshes[out->count];mesh.indices.resize(partition.retained.size()/3);
				std::memcpy(mesh.indices.data(),partition.retained.data(),partition.retained.size()*2);
				surface.flags&=~4u;std::fill(std::begin(surface.blendVertCounts),std::end(surface.blendVertCounts),0);
				surface.blendVerts=nullptr;surface.blendVertsTable=nullptr;surface.blendVertsBuffer=nullptr;surface.blendVertsView=nullptr;
				out->surfaces[out->count]=surface;
				if(!read(model.materialHandles+n,out->materials[out->count]) || !out->materials[out->count])return false;
				++out->count;continue;
			}
			// A receiver can contain an unrelated skinned strap. Its native part
			// mask must prove separation; never discard a selected skinned part.
			if (surface.flags&4u)
			{
				if ((surface.flags&~7u) || std::all_of(std::begin(surface.partBits),std::end(surface.partBits),[](int v){return !v;})) return false;
				for (auto b:bones) if (unsigned(surface.partBits[b/32])&(0x80000000u>>(b%32))) return false;
				continue;
			}
			if ((surface.flags&~3u) || surface.subdivLevelCount ||
				surface.subdiv || surface.blendShapesCount || !surface.rigidVertListCount || surface.rigidVertListCount>32 ||
				!surface.rigidVertLists || !surface.triIndices || !surface.vb0 || !surface.indexBuffer) return false;
			unsigned vertices=0,triangles=0;
			auto& mesh=out->meshes[out->count];
			for (unsigned k=0;k<surface.rigidVertListCount;++k)
			{
				game::XRigidVertList group;
				if (!read(surface.rigidVertLists+k,group) || group.boneOffset%64 ||
					group.boneOffset/64>=model.numBones || group.triOffset!=triangles ||
					group.vertCount>surface.vertCount-vertices || group.triCount>surface.triCount-triangles) return false;
				if (std::find(bones.begin(),bones.end(),unsigned(group.boneOffset/64))!=bones.end() && group.triCount)
				{
					const auto selected=std::find(bones.begin(),bones.end(),unsigned(group.boneOffset/64))-bones.begin();
					seen|=1u<<selected;
					const auto begin=mesh.indices.size();
					mesh.indices.resize(begin+group.triCount);
					if (!utils::native_memory::read_bytes(mesh.indices.data()+begin,surface.triIndices+group.triOffset,group.triCount*sizeof(game::Face))) return false;
					for (const auto& f : std::span(mesh.indices).subspan(begin))
						for (auto index : {f.v1,f.v2,f.v3}) if (index<vertices || index>=vertices+group.vertCount) return false;
					if(!face_ranges.empty())
					{
						auto retained=begin;
						for(unsigned f=0;f<group.triCount;++f)
						{
							const auto id=group.triOffset+f;
							const bool match=face_in_partition(face_ranges,n,id);
							if(match)++matched_faces;
							if(match==include)mesh.indices[retained++]=mesh.indices[begin+f];
						}
						mesh.indices.resize(retained);
					}
					// Native R_FilterXModelIntoScene draws a rigid surface with ONE
					// placement. Its indices are surface-global (live verified), so
					// retain the original vertex layout and select only this index run.
				}
				vertices+=group.vertCount; triangles+=group.triCount;
			}
			if (vertices!=surface.vertCount || triangles!=surface.triCount) return false;
			if (!mesh.indices.empty())
			{
				out->surfaces[out->count]=surface;
				if (!read(model.materialHandles+n,out->materials[out->count]) || !out->materials[out->count]) return false;
				++out->count;
			}
		}
		if (matched_faces!=expected_faces || seen!=((1u<<bones.size())-1) || !out->count || !read(model.boneNames+bone,out->name)) return false;
		game::Bounds bounds{};
		float low[3]{FLT_MAX,FLT_MAX,FLT_MAX},high[3]{-FLT_MAX,-FLT_MAX,-FLT_MAX},radius_squared{};
		for (unsigned n=0;n<out->count;++n)
		{
			auto& surface=out->surfaces[n];
			auto& mesh=out->meshes[n];
			status_="rigid part CPU vertex/bounds contract rejected";
			mesh.vertices.resize(surface.vertCount);
			if (!utils::native_memory::read_bytes(mesh.vertices.data(),surface.verts0.packedVerts0,
				mesh.vertices.size()*sizeof(game::GfxPackedVertex))) return false;
			surface.verts0.packedVerts0=mesh.vertices.data();
			for (const auto& face : mesh.indices) for (auto index : {face.v1,face.v2,face.v3})
			{
				const auto& xyz=mesh.vertices[index].xyz; float squared{};
				for (int c=0;c<3;++c)
				{
					if (!std::isfinite(xyz[c]) || std::abs(xyz[c])>10000) return false;
					low[c]=std::min(low[c],xyz[c]); high[c]=std::max(high[c],xyz[c]); squared+=xyz[c]*xyz[c];
				}
				radius_squared=std::max(radius_squared,squared);
			}
			ComPtr<ID3D11Device> device; surface.vb0->GetDevice(&device);
			status_="rigid part D3D device unavailable";
			if (!device) return false;
			status_="rigid part index buffer/SRV format or allocation rejected";
			if (!create_surface_indices(device.Get(),surface.indexBufferView,
				std::as_bytes(std::span(mesh.indices)),mesh.index_buffer,mesh.index_view)) return false;
			mesh.vertex_buffer=surface.vb0; mesh.vertex_view=surface.vb0View;
			surface.indexBuffer=mesh.index_buffer.Get(); surface.indexBufferView=mesh.index_view.Get();
			surface.triCount=static_cast<unsigned short>(mesh.indices.size());
			surface.triIndices=surface.triIndices2=mesh.indices.data();
			mesh.group={0,surface.vertCount,0,surface.triCount,nullptr};
			surface.rigidVertLists=&mesh.group; surface.rigidVertListCount=1;
			std::fill(std::begin(surface.partBits),std::end(surface.partBits),0); surface.partBits[0]=int(0x80000000u);
		}
		for (int c=0;c<3;++c) { bounds.midPoint[c]=(low[c]+high[c])*.5f; bounds.halfSize[c]=(high[c]-low[c])*.5f; }
		out->lod.name="vr_runtime_rigid_part"; out->lod.surfs=out->surfaces.data(); out->lod.numsurfs=static_cast<unsigned short>(out->count);
		out->lod.partBits[0]=int(0x80000000u);
		// Fresh minimal model metadata: no collision, physics, MDAO, reactive
		// motion or source skeleton pointers can be consumed by the rigid route.
		auto& m=out->model;
		m.name=out->lod.name; m.numBones=m.numRootBones=1; m.numsurfs=static_cast<unsigned char>(out->count); m.numLods=1;
		m.scale=model.scale; m.unk_float=model.unk_float; m.boneNames=&out->name; m.baseMat=&out->identity;
		m.boneInfo=&out->bone_info; m.materialHandles=out->materials.data(); m.quantization=model.quantization;
		m.radius=std::sqrt(radius_squared); m.bounds=bounds;
		out->bone_info.bounds=bounds; out->bone_info.radiusSquared=radius_squared;
		m.lodInfo[0].dist=model.lodInfo[0].dist; m.lodInfo[0].numsurfs=m.numsurfs;
		m.lodInfo[0].surfs=out->surfaces.data(); m.lodInfo[0].modelSurfs=&out->lod; m.lodInfo[0].partBits[0]=int(0x80000000u);
		std::copy(std::begin(bind.trans),std::end(bind.trans),out->bind.begin());
		std::copy(std::begin(bind.quat),std::end(bind.quat),out->bind.begin()+3);
		data_=std::move(out); status_="ready"; return true;
	}
}
