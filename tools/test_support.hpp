#pragma once
#include<algorithm>
#include<cmath>
#include<stdexcept>

inline void check(bool ok, const char* message)
{
	if(!ok)
	{
		throw std::runtime_error(message);
	}
}

inline bool near(float actual, float expected, float absTol = 1e-5f, float relTol = 1e-4f) {
	return std::isfinite(actual) && std::isfinite(expected) &&
		std::abs(actual - expected) <=
		absTol + relTol * std::abs(expected);
}