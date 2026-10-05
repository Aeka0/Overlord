#pragma once
namespace vr::gameplay::sequences::body
{
	void prepare();
	void apply(const void* object,const void* matrices) noexcept;
	void invalidate() noexcept;
}
