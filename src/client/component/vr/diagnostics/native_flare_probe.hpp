#pragma once
namespace vr::diagnostics::native_flare_probe
{
	using emit_fn = void(*)(void*, const float*, const float*, const float*);
	using draw_fn = void(*)(void*, void*, unsigned);
#ifdef DEBUG
	void emit(emit_fn original, void* context, const float* center, const float* corner1, const float* corner2);
	void draw(draw_fn original, void* context, void* record, unsigned technique);
#else
	inline void emit(emit_fn original, void* c, const float* p, const float* a, const float* b) { original(c,p,a,b); }
	inline void draw(draw_fn original, void* c, void* r, unsigned t) { original(c,r,t); }
#endif
}
