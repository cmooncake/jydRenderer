#include "renderer.hpp"

#include <algorithm>
#include <cmath>

namespace jyd {

namespace {

void swapInt(int& a, int& b) {
    std::swap(a, b);
}

int clampInt(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

double area(double ax, double ay, double bx, double by, double cx, double cy){
    return .5*(ax*(by-cy)+bx*(cy-ay)+cx*(ay-by));
}

} // namespace

Renderer::Renderer(Framebuffer& framebuffer)
    : framebuffer_(framebuffer)
    , camera(framebuffer_.width(), framebuffer_.height()) {
    zbuffer_.resize(framebuffer_.width() * framebuffer_.height(), -1.0);
}

void Renderer::clear(const Color& color)
{
    framebuffer_.clear(color);
    zbuffer_.assign(zbuffer_.size(), -1.0);
}

double Renderer::getZbuffer(int x, int y)
{
    if (x < 0 || y < 0 || x >= framebuffer_.width() || y >= framebuffer_.height()) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(y) * framebuffer_.width() + x ;
    return zbuffer_[index];
}

void Renderer::setZbuffer(int x, int y, double zbuf)
{
    if (x < 0 || y < 0 || x >= framebuffer_.width() || y >= framebuffer_.height()) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(y) * framebuffer_.width() + x;
    zbuffer_[index] = zbuf;
}

void Renderer::drawLine(int x0, int y0, int x1, int y1, const Color& color) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        framebuffer_.setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }

        const int error2 = 2 * error;
        if (error2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (error2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void Renderer::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const Color& color) {
    if (y0 > y1) {
        swapInt(y0, y1);
        swapInt(x0, x1);
    }
    if (y1 > y2) {
        swapInt(y1, y2);
        swapInt(x1, x2);
    }
    if (y0 > y1) {
        swapInt(y0, y1);
        swapInt(x0, x1);
    }

    const int totalHeight = y2 - y0;
    if (totalHeight == 0) {
        return;
    }

    for (int y = y0; y <= y2; ++y) {
        const bool secondHalf = y > y1 || y1 == y0;
        const int segmentHeight = secondHalf ? y2 - y1 : y1 - y0;
        if (segmentHeight == 0) {
            continue;
        }

        const float alpha = static_cast<float>(y - y0) / totalHeight;
        const float beta = secondHalf
            ? static_cast<float>(y - y1) / segmentHeight
            : static_cast<float>(y - y0) / segmentHeight;

        const int startX = static_cast<int>(x0 + (x2 - x0) * alpha);
        const int endX = secondHalf
            ? static_cast<int>(x1 + (x2 - x1) * beta)
            : static_cast<int>(x0 + (x1 - x0) * beta);

        if (startX > endX) {
            for (int x = endX; x <= startX; ++x) {
                framebuffer_.setPixel(x, y, color);
            }
        } else {
            for (int x = startX; x <= endX; ++x) {
                framebuffer_.setPixel(x, y, color);
            }
        }
    }
}


void Renderer::drawTriangle_barycentric(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const Color& color)
{
    int lb = std::min(std::min(x0,x1),x2), rb = std::max(std::max(x0, x1), x2);
    int bb = std::min(std::min(y0, y1), y2), tb = std::max(std::max(y0, y1), y2);
    double abc = area(x0, y0, x1, y1, x2, y2);
    if (abc < 1e-10) return;
    for (int y = bb; y <= tb; y++)
    {
        for (int x = lb; x <= rb; x++)
        {
            double alpha = area(x, y, x1, y1, x2, y2)/ abc;
            double beta = area(x, y, x2, y2, x0, y0)/ abc;
            double gamma = area(x, y, x0, y0, x1, y1)/ abc;
            if (alpha < 0 || beta < 0 || gamma < 0)
                continue;
            double zbuf = alpha * z0 + beta * z1 + gamma *z2;
            double z_current = getZbuffer(x, y);
            if (zbuf > z_current)
            {
                setZbuffer(x, y, zbuf);
                //framebuffer_.setPixel(x, y, { static_cast < std::uint8_t>(color.r*alpha),  static_cast < std::uint8_t>(color.g *beta) ,  static_cast < std::uint8_t>(color.b*gamma), color.a});
                framebuffer_.setPixel(x, y, { static_cast <std::uint8_t>(0.5 * (zbuf + 1.)*(float)color.r ),  static_cast <std::uint8_t>(0.5 * (zbuf + 1.) * (float)color.g) ,  static_cast <std::uint8_t>(0.5 * (zbuf + 1.) * (float)color.b),color.a });

            }
        }
    }
}

void Renderer::drawModel(const Model& model)
{
    const mat4 mvp = camera.projectionMatrix() * camera.viewTransformation();
    const double halfW = static_cast<double>(framebuffer_.width()) * 0.5;
    const double halfH = static_cast<double>(framebuffer_.height()) * 0.5;

    for (const auto& face : model.facet) {
        vec3 world[3] = {
            vec3(model.verts[face[0]][0], model.verts[face[0]][1], model.verts[face[0]][2]),
            vec3(model.verts[face[1]][0], model.verts[face[1]][1], model.verts[face[1]][2]),
            vec3(model.verts[face[2]][0], model.verts[face[2]][1], model.verts[face[2]][2]),
        };

        vec3 ndc[3];
        for (int i = 0; i < 3; ++i) {
            vec4 clip = mvp * vec4(world[i], 1.0);
            if (clip[3] <= 0.0) {
                ndc[i] = vec3(0, 0, -2.0);
                continue;
            }
            clip = clip / clip[3];
            ndc[i] = vec3(clip[0], clip[1], clip[2]);
        }

        const int x0 = static_cast<int>((ndc[0][0] + 1.0) * halfW);
        const int y0 = static_cast<int>((1.0 - ndc[0][1]) * halfH);
        const int x1 = static_cast<int>((ndc[1][0] + 1.0) * halfW);
        const int y1 = static_cast<int>((1.0 - ndc[1][1]) * halfH);
        const int x2 = static_cast<int>((ndc[2][0] + 1.0) * halfW);
        const int y2 = static_cast<int>((1.0 - ndc[2][1]) * halfH);

        drawTriangle_barycentric(
            x0, y0, ndc[0][2],
            x1, y1, ndc[1][2],
            x2, y2, ndc[2][2],
            {211, 211, 211, 255});
    }
}

} // namespace jyd
