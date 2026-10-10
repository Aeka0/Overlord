#pragma once
#include "../native_post_aa_contract.hpp"
#include "../native_display_contract.hpp"
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <sstream>

namespace vr::diagnostics::post_aa
{
	// Values only: retained reports must neither keep GPU resources alive nor
	// query native registry pointers after device/session teardown.
	struct target
	{
		std::uint32_t id{};
		std::uintptr_t image{}, map{}, rtv{}, srv{}, output_resource{}, input_resource{}, texture_identity{}, device{};
		bool texture_known{}, rtv_known{}, srv_known{};
		HRESULT texture_query{E_NOINTERFACE};
		D3D11_TEXTURE2D_DESC texture{};
		D3D11_RENDER_TARGET_VIEW_DESC output{};
		D3D11_SHADER_RESOURCE_VIEW_DESC input{};
	};

	// Called only at a failed validation, under the existing native GPU owner.
	// GetDesc/GetResource copy metadata; no GPU readback or synchronization.
	inline target observe(std::uint32_t id, const void* image, ID3D11Texture2D* map,
		ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* srv)
	{
		target value;
		value.id = id; value.image = reinterpret_cast<std::uintptr_t>(image);
		value.map = reinterpret_cast<std::uintptr_t>(map);
		value.rtv = reinterpret_cast<std::uintptr_t>(rtv); value.srv = reinterpret_cast<std::uintptr_t>(srv);
		Microsoft::WRL::ComPtr<ID3D11Resource> output, input;
		if (rtv) { rtv->GetResource(&output); rtv->GetDesc(&value.output); value.rtv_known = true; }
		if (srv) { srv->GetResource(&input); srv->GetDesc(&value.input); value.srv_known = true; }
		value.output_resource = reinterpret_cast<std::uintptr_t>(output.Get());
		value.input_resource = reinterpret_cast<std::uintptr_t>(input.Get());
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (output) value.texture_query = output.As(&texture);
		if (texture)
		{
			value.texture_identity = reinterpret_cast<std::uintptr_t>(texture.Get());
			texture->GetDesc(&value.texture); value.texture_known = true;
			Microsoft::WRL::ComPtr<ID3D11Device> owner;
			texture->GetDevice(&owner); value.device = reinterpret_cast<std::uintptr_t>(owner.Get());
		}
		return value;
	}

	inline const char* identity_rejection(const target& value) noexcept
	{
		if (!value.image) return "image_missing";
		if (!value.rtv) return "rtv_missing";
		if (!value.srv) return "srv_missing";
		if (!value.output_resource) return "rtv_resource_missing";
		if (!value.input_resource) return "srv_resource_missing";
		if (value.output_resource != value.input_resource) return "rtv_srv_resource_mismatch";
		if (!value.texture_known) return "texture_interface";
		if (value.map != value.texture_identity) return "image_map_mismatch";
		return nullptr;
	}

	inline const char* rejection(const target& value, const target& reference) noexcept
	{
		if (const auto reason = identity_rejection(value)) return reason;
		if (!reference.texture_known) return "reference_unavailable";
		if (value.device != reference.device) return "device_mismatch";
		return native_post_aa::ldr_rejection(value.texture, value.output, value.input,
			reference.texture.Width, reference.texture.Height);
	}

	struct failure
	{
		std::uint64_t sequence{}, tick{};
		std::uint32_t thread{};
		std::uintptr_t record{};
		native_post_aa::mode selected{};
		native_post_aa::view_identity view{};
		native_display_contract::route route{};
		const char* stage{"none"};
		const char* detail{"unavailable"};
		bool targets_known{}, peer_known{};
		target reference{}, failed{}, peer{};
	};

	inline const char* mode_name(native_post_aa::mode value) noexcept
	{
		constexpr std::array names{"off", "fxaa", "smaa", "smaa_t2x", "filmic_smaa", "filmic_smaa_t2x", "fxaa_internal"};
		const auto index = static_cast<std::size_t>(value);
		return index < names.size() ? names[index] : "unsupported";
	}

	class history
	{
		std::mutex mutex_;
		failure first_, latest_;
		std::atomic_uint64_t total_{}, dropped_{};
	public:
		void record(failure value) noexcept
		{
			value.sequence = ++total_;
			const std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock()) { ++dropped_; return; }
			if (!first_.sequence) first_ = value;
			latest_ = value;
		}
		std::string format(std::uint64_t now)
		{
			failure first, latest; bool ready{};
			{
				const std::unique_lock lock(mutex_, std::try_to_lock);
				ready = lock.owns_lock();
				if (ready) { first = first_; latest = latest_; }
			}
			std::ostringstream out;
			out << "  native_post_aa_validation: scope=process failures=" << total_.load()
				<< " dropped_samples=" << dropped_.load() << " snapshot_busy=" << !ready << '\n';
			if (!ready) return out.str();
			const auto append_target = [&](const char* role, const target& t)
			{
				out << "      " << role << ": target=" << t.id << " image=0x" << std::hex << t.image
					<< " map=0x" << t.map << " rtv=0x" << t.rtv << " srv=0x" << t.srv
					<< " rtv_resource=0x" << t.output_resource << " srv_resource=0x" << t.input_resource
					<< " texture=0x" << t.texture_identity << " device=0x" << t.device
					<< " texture_query=0x" << static_cast<std::uint32_t>(t.texture_query)
					<< std::dec << " texture_known=" << t.texture_known << '\n';
				if (t.texture_known)
					out << "        texture: size=" << t.texture.Width << 'x' << t.texture.Height
						<< " format=" << t.texture.Format << " mips=" << t.texture.MipLevels << " array=" << t.texture.ArraySize
						<< " samples=" << t.texture.SampleDesc.Count << ':' << t.texture.SampleDesc.Quality
						<< " usage=" << t.texture.Usage << " bind=0x" << std::hex << t.texture.BindFlags
						<< " cpu=0x" << t.texture.CPUAccessFlags << " misc=0x" << t.texture.MiscFlags << std::dec << '\n';
				out << "        views: rtv_known=" << t.rtv_known << " srv_known=" << t.srv_known;
				if (t.rtv_known) out << " rtv_format=" << t.output.Format << " rtv_dimension=" << t.output.ViewDimension;
				if (t.rtv_known && t.output.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D)
					out << " rtv_mip=" << t.output.Texture2D.MipSlice;
				if (t.srv_known) out << " srv_format=" << t.input.Format << " srv_dimension=" << t.input.ViewDimension;
				if (t.srv_known && t.input.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)
					out << " srv_mips=" << t.input.Texture2D.MostDetailedMip << ':' << t.input.Texture2D.MipLevels;
				out << '\n';
			};
			const auto append = [&](const char* label, const failure& f)
			{
				out << "    " << label << ": sequence=" << f.sequence << " tick=" << f.tick
					<< " age_ms=" << (now >= f.tick ? now - f.tick : 0) << " thread=" << f.thread
					<< " stage=" << f.stage << " detail=" << f.detail << " mode=" << mode_name(f.selected)
					<< '(' << static_cast<unsigned>(f.selected) << ") pair=" << f.view.pair << " eye=" << f.view.eye
					<< " generation=" << f.view.device_generation << " route=" << f.route.source << "->" << f.route.destination
					<< " record=0x" << std::hex << f.record << std::dec << " targets_known=" << f.targets_known << '\n';
				if (!f.targets_known) return;
				if (f.failed.id != f.reference.id)
					out << "      expected_ldr: extent_known=" << f.reference.texture_known
						<< " size=" << f.reference.texture.Width << 'x' << f.reference.texture.Height
						<< " texture_format=RGBA8_TYPELESS(27)/RGBA8_UNORM(28) view_format=RGBA8_UNORM(28)"
						<< " array=1 mips=1 samples=1:0 usage=0 required_bind=0x28 cpu=0 misc=0\n";
				else out << "      expected_display: texture_format=R11G11B10_FLOAT(26) nonzero_extent=1"
					<< " array=1 mips=1 samples=1:0 usage=0 exact_bind=0xa8 cpu=0 misc=0\n";
				append_target("reference", f.reference); append_target("failed", f.failed);
				if (f.peer_known) append_target("smaa_peer", f.peer);
			};
			if (first.sequence) append("first_failure", first);
			if (latest.sequence != first.sequence) append("latest_failure", latest);
			return out.str();
		}
	};
}
