#pragma once
#include "vec.hpp"
#include <algorithm>
#include <cmath>

namespace jyd
{
	inline bool finite3(vec3 v) {
		return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
	}

	inline vec3 safeNormalize(vec3 v) {
		const float l2 = v * v;
		if (!std::isfinite(l2) || l2 <= 1e-16f) return vec3(0);
		return v / std::sqrt(l2);
	}

	inline vec3 mul(vec3 a, vec3 b) {
		return vec3(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
	}

	inline vec3 saturate3(vec3 v) {
		return { std::clamp(v.x, 0.0f, 1.0f),
				std::clamp(v.y, 0.0f, 1.0f),
				std::clamp(v.z, 0.0f, 1.0f) };
	}
}