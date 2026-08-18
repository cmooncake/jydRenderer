#pragma once

#include "framebuffer.hpp"
#include "camera.hpp"
#include "model.hpp"
#include "texture.hpp"

#include <algorithm>
#include <cmath>

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
            vec4 pos =  mvp * vec4(vertex.position, 1.0);
            vec4 n = normalize(vp * vec4(vertex.normal, 0.0));
			
			return { pos, vec3(n), vertex.texcoord };
        }
        bool fragment(const Commonv2f& f, Color& color) const override {
            if (texture == nullptr) {
                // Magenta makes a missing texture binding obvious.
                color = { 255, 0, 255, 255 };
                return false;
            }

            const Color texel = texture->sampleNearest(f.texcoord);
            const vec3 texColor = vec3(texel.r, texel.g, texel.b) / 255.0;

            // Retain the simple diffuse light with a small ambient component.
            const double diffuse = DiffuseLightDirection * vec3(-f.normal);
			const double diffuseClamped = std::clamp(diffuse, 0.0, 1.0);
			
			double diffuseFactor = 0.8;
            vec3 diffuseColor = vec3(1.0f, 1.0f, 1.0f) *  diffuseClamped * diffuseFactor;

			vec3 viewDir = normalize(cameraPosition - vec3(f.position));
			vec3 halfwayDir = normalize(SpecularLightDirection + viewDir);
			const double specular = std::pow(std::max(0.0, vec3(-f.normal) * halfwayDir), 32.0);
			const double specularFactor = 0.0; // Adjust this value to control the specular intensity
			vec3 specularColor = vec3(1.0, 1.0, 1.0) * specular * specularFactor;

			double lighting = diffuseClamped * diffuseFactor + specular * specularFactor + 0.2; // Add ambient component
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


class Renderer {
public:
    explicit Renderer(Framebuffer& framebuffer);

    Camera& getCamera() { return camera; }
	inline vec3 SpecularLightDirection() const { return normalize(vec3(1.0, 1.0, 1.0)); }
    inline vec3 DiffuseLightDirection() const { return normalize(vec3(0.0, 0.0, -1.0)); }
    inline vec3 AmbientLightColor() const { return vec3(0.8, 0.8, 0.8); }

    void clear(const Color& color);
    double getZbuffer(int x, int y);
    void setZbuffer(int x, int y, double zbuf);
    void drawLine(int x0, int y0, int x1, int y1, const Color& color);
    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const Color& color);
    void drawTriangle_barycentric(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const Color& color);
    void drawTriangle_byShader(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const CommonShader& shader, const Commonv2f (&vertices)[3]);
    void drawModel(const Model& model);

	int Pipeline(const Model& model, struct CommonShader& shader);


private:
    Framebuffer& framebuffer_;
    std::vector<double> zbuffer_;
    Camera camera;
};

} // namespace jyd
