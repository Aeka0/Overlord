#pragma once
#include "../../eye_composition.hpp"

namespace vr::gameplay::vehicles
{
	void begin_scene(const engine_stereo_view::slot_pair&,std::uintptr_t) noexcept;
	void present_hud(const eye_composition::event&,ID3D11DeviceContext*,ID3D11ShaderResourceView*,ID3D11RenderTargetView*) noexcept;
}
