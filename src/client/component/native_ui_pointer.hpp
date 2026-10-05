#pragma once

namespace input
{
	// Native UI polls absolute desktop coordinates, even when nothing moved.
	// Keep that baseline independent from coordinates delivered by a VR ray.
	class native_ui_pointer
	{
		int x_{},y_{};bool observed_{},vr_owned_{};
	public:
		struct decision {bool activity{},forward{};};
		struct position {int x{},y{};bool restore{};};
		decision native_position(int x,int y) noexcept
		{
			const bool moved=observed_&&(x!=x_||y!=y_);
			x_=x;y_=y;observed_=true;
			if(moved)vr_owned_=false;
			return {moved,!vr_owned_};
		}
		void acquire() noexcept {vr_owned_=true;}
		void release() noexcept {vr_owned_=false;}
		position native_button() noexcept
		{
			const position out{x_,y_,observed_&&vr_owned_};
			vr_owned_=false;return out;
		}
	};
}
