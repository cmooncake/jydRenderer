#pragma once

#include <algorithm>
#include <cmath>

#include "texture.hpp"
#include "render_math.hpp"

namespace jyd {

    template<typename a2v, typename v2f>
    struct IShader {
        virtual v2f vertex(const a2v& a) const = 0;
        virtual bool fragment(const v2f& v, Color& color) const = 0;

        virtual ~IShader() = default;
    };

    struct Commona2v {
        vec3 position;
        vec3 normal;
        vec2 texcoord;
    };

    struct Commonv2f {
        vec4 position;      // Clip space; not a world-space lighting position.
        vec3 normal;        // World space, normalized only at fragment shading.
        vec2 texcoord;
        vec3 worldPosition; // Model is identity until model transforms are added.
    };


    template<typename Varying>
    struct VaryingTraits;

    template<>
    struct VaryingTraits<Commonv2f> {
        static const vec4& clipPosition(const Commonv2f& value) {
            return value.position;
        }

        static Commonv2f interpolateEdge(
            const Commonv2f& a, const Commonv2f& b, float t) {
            return {
                a.position + (b.position - a.position) * t,
                a.normal + (b.normal - a.normal) * t,
                a.texcoord + (b.texcoord - a.texcoord) * t,
                a.worldPosition + (b.worldPosition - a.worldPosition) * t
            };
        }

        static Commonv2f interpolateFragment(
            const Commonv2f(&v)[3], float a, float b, float c) {
            // 调用方保留 w 有限、非零和分母有效性检查。
            const float q0 = a / v[0].position.w;
            const float q1 = b / v[1].position.w;
            const float q2 = c / v[2].position.w;
            const float s = q0 + q1 + q2;
            return {
                (v[0].position * q0 + v[1].position * q1 + v[2].position * q2) / s,
                (v[0].normal * q0 + v[1].normal * q1 + v[2].normal * q2) / s,
                (v[0].texcoord * q0 + v[1].texcoord * q1 + v[2].texcoord * q2) / s,
                (v[0].worldPosition * q0 + v[1].worldPosition * q1 +
                 v[2].worldPosition * q2) / s
            };
        }
    };

    struct CommonShader : public IShader<Commona2v, Commonv2f> {
        mat4 mvp;
        mat4 vp;
        const Texture* texture = nullptr;
        vec3 cameraPosition;

        vec3 SpecularLightDirection;
        vec3 DiffuseLightDirection;
        vec3 AmbientLightColor;

        virtual const char* name() const = 0;
        virtual void prepare() {}
    };

    struct NativeCommonShader final : public CommonShader {
        const char* name() const override {
            return "Native Common";
        }

        Commonv2f vertex(const Commona2v& a) const override {
            return {mvp * vec4(a.position, 1.0f),
                    a.normal, a.texcoord, a.position};
        }
        bool fragment(const Commonv2f& f, Color& color) const override {
            if (texture == nullptr) {
                // Magenta makes a missing texture binding obvious.
                color = { 255, 0, 255, 255 };
                return false;
            }

            const Color texel = texture->sampleNearest(f.texcoord);
            const vec3 n = safeNormalize(f.normal);
            // Legacy Common lighting, not PBR. Light points toward the source.
            const float diffuse = std::clamp(n * DiffuseLightDirection, 0.0f, 1.0f);
            const float lighting = diffuse * 0.8f + 0.2f;
            color = {
                static_cast<std::uint8_t>(texel.r * lighting),
                static_cast<std::uint8_t>(texel.g * lighting),
                static_cast<std::uint8_t>(texel.b * lighting),
                texel.a
            };
            return false;
        }
    };
}
