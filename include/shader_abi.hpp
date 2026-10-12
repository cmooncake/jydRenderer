#pragma once

#include <cstdint>

namespace jyd::shader_abi {
	inline constexpr std::uint32_t Version = 1;

	namespace VertexIn
	{
		inline constexpr int Position = 0, Normal = 3, Texcoord = 6, Count = 8;
	}

	namespace Varying {
		inline constexpr int Position = 0, Normal = 4, Texcoord = 7, Count = 9;
	}
	namespace Uniform {
		inline constexpr int Mvp = 0, View = 16, Camera = 32;
		inline constexpr int SpecularLight = 35, DiffuseLight = 38, Ambient = 41;
		inline constexpr int Count = 44;
	}
}
