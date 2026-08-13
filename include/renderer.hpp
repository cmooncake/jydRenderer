#pragma once

#include "framebuffer.hpp"
#include "camera.hpp"
#include "model.hpp"

namespace jyd {
    struct IShader {
        virtual vec4 vertex(const vec3& vertex) const = 0;
		virtual bool fragment(const vec3& barycentric, Color& color) const = 0;

        virtual ~IShader() = default;
    };

    struct CommonShader : public IShader {
        mat4 mvp;
        vec4 vertex(const vec3& vertex) const override {
            return mvp * vec4(vertex, 1.0);
        }
        bool fragment(const vec3& barycentric, Color& color) const override {
            color = { static_cast<std::uint8_t>(barycentric[0] * 211),
                      static_cast<std::uint8_t>(barycentric[1] * 211),
                      static_cast<std::uint8_t>(barycentric[2] *74),
                      255 };
            return true;
        }
    };


class Renderer {
public:
    explicit Renderer(Framebuffer& framebuffer);

    Camera& getCamera() { return camera; }

    void clear(const Color& color);
    double getZbuffer(int x, int y);
    void setZbuffer(int x, int y, double zbuf);
    void drawLine(int x0, int y0, int x1, int y1, const Color& color);
    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const Color& color);
    void drawTriangle_barycentric(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const Color& color);
    void drawTriangle_byShader(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const IShader& shader);
    void drawModel(const Model& model);

	void Pipeline(const Model& model, struct CommonShader& shader);

private:
    Framebuffer& framebuffer_;
    std::vector<double> zbuffer_;
    Camera camera;
};

} // namespace jyd
