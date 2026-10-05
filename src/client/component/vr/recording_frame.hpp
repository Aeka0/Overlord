#pragma once
namespace vr::recording_frame
{
	// GUI-owner publication for the NEXT produced eye. Tiny value copy only.
	void publish_viewport(float width, float height, float horizontal_fov) noexcept;
}
