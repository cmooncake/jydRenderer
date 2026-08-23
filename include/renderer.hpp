#pragma once

#include "framebuffer.hpp"
#include "camera.hpp"
#include "model.hpp"
#include "texture.hpp"
#include "shader.hpp"

#include <algorithm>
#include <cmath>

namespace jyd {
    enum RenderMod{
        DepthMap = 0,
        Filling = 1,
		Nolighting = 2,
		Lighting = 3,
        Wireframe = 4
    };

	const int ModTypesCount = 5;


class Renderer {
public:
    explicit Renderer(Framebuffer& framebuffer);

    Camera& getCamera() { return camera; }
	inline vec3 SpecularLightDirection() const { return normalize(vec3(1.0f, 1.0f, 1.0f)); }
    inline vec3 DiffuseLightDirection() const { return normalize(vec3(0.0f, 0.0f, -1.0f)); }
    inline vec3 AmbientLightColor() const { return vec3(0.8f, 0.8f, 0.8f); }

    void clear(const Color& color);
    float getZbuffer(int x, int y);
    void setZbuffer(int x, int y, float zbuf);
    void drawLine(int x0, int y0, int x1, int y1, const Color& color);
    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const Color& color);
    void drawTriangle_barycentric(int x0, int y0, float z0, int x1, int y1, float z1, int x2, int y2, float z2, const Color& color);
    void drawTriangle_byShader(int x0, int y0, float z0, int x1, int y1, float z1, int x2, int y2, float z2, const CommonShader& shader, const Commonv2f (&vertices)[3]);
    void drawModel(const Model& model);

	int Pipeline(const Model& model, struct CommonShader& shader, RenderMod mod);


private:
    Framebuffer& framebuffer_;
    std::vector<float> zbuffer_;
    Camera camera;
};

} // namespace jyd
