#pragma once

#include <algorithm>
#include <cmath>

#include "texture.hpp"

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
        vec4 position;
        vec3 normal;
        vec2 texcoord;
    };

    struct CommonShader : public IShader<Commona2v, Commonv2f> {
        mat4 mvp;
        mat4 vp;
        const Texture* texture = nullptr;
        vec3 cameraPosition;

        vec3 SpecularLightDirection;
        vec3 DiffuseLightDirection;
        vec3 AmbientLightColor;

        Commonv2f vertex(const Commona2v& vertex) const override {
            vec4 pos = mvp * vec4(vertex.position, 1.0f);
            vec4 n = normalize(vp * vec4(vertex.normal, 0.0f));

            return { pos, vec3(n), vertex.texcoord };
        }
        bool fragment(const Commonv2f& f, Color& color) const override {
            if (texture == nullptr) {
                // Magenta makes a missing texture binding obvious.
                color = { 255, 0, 255, 255 };
                return false;
            }

            const Color texel = texture->sampleNearest(f.texcoord);
            const vec3 texColor = vec3(texel.r, texel.g, texel.b) / 255.0f;

            // Retain the simple diffuse light with a small ambient component.
            const float diffuse = DiffuseLightDirection * vec3(-f.normal);
            const float diffuseClamped = std::clamp(diffuse, 0.0f, 1.0f);

            const float diffuseFactor = 0.8f;
            vec3 diffuseColor = vec3(1.0f, 1.0f, 1.0f) * diffuseClamped * diffuseFactor;

            vec3 viewDir = normalize(cameraPosition - vec3(f.position));
            vec3 halfwayDir = normalize(SpecularLightDirection + viewDir);
            const float specular = std::pow(std::max(0.0f, vec3(-f.normal) * halfwayDir), 32.0f);
            const float specularFactor = 0.0f; // Adjust this value to control the specular intensity
            vec3 specularColor = vec3(1.0f, 1.0f, 1.0f) * specular * specularFactor;

            const float lighting = diffuseClamped * diffuseFactor + specular * specularFactor + 0.2f; // Add ambient component
            vec3 finalColor = texColor * lighting;


            color = {
                static_cast<std::uint8_t>(finalColor[0] * 255),
                static_cast<std::uint8_t>(finalColor[1] * 255),
                static_cast<std::uint8_t>(finalColor[2] * 255),
                texel.a
            };
            return false;
        }
    };
}