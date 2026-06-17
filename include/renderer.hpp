#pragma once

#include "framebuffer.hpp"
#include "camera.hpp"
#include "model.hpp"

namespace jyd {

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
    void drawModel(const Model& model);

private:
    Framebuffer& framebuffer_;
    std::vector<double> zbuffer_;
    Camera camera;
};

} // namespace jyd
