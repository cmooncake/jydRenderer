#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <limits>

namespace jyd {

namespace {

void swapInt(int& a, int& b) {
    std::swap(a, b);
}

int clampInt(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

float area(int ax, int ay, int bx, int by, int cx, int cy) {
    const float fax = static_cast<float>(ax);
    const float fay = static_cast<float>(ay);
    const float fbx = static_cast<float>(bx);
    const float fby = static_cast<float>(by);
    const float fcx = static_cast<float>(cx);
    const float fcy = static_cast<float>(cy);
    return 0.5f * (fax * (fby - fcy) + fbx * (fcy - fay) + fcx * (fay - fby));
}

float clipDistance(const vec4& vertex, int plane) {
    switch (plane) {
    case 0: return vertex[0] + vertex[3];
    case 1: return vertex[3] - vertex[0];
    case 2: return vertex[1] + vertex[3];
    case 3: return vertex[3] - vertex[1];
    case 4: return vertex[2] + vertex[3];
    case 5: return vertex[3] - vertex[2];
    default: return -1.0f;
    }
}

std::vector<vec4> clipAgainstPlane(const std::vector<vec4>& polygon, int plane) {
    std::vector<vec4> output;
    if (polygon.empty()) {
        return output;
    }

    vec4 previous = polygon.back();
    float previousDistance = clipDistance(previous, plane);
    bool previousInside = previousDistance >= 0.0f;

    for (const vec4& current : polygon) {
        const float currentDistance = clipDistance(current, plane);
        const bool currentInside = currentDistance >= 0.0f;

        if (currentInside != previousInside) {
            const float t = previousDistance /
                (previousDistance - currentDistance);
            output.push_back(previous + (current - previous) * t);
        }
        if (currentInside) {
            output.push_back(current);
        }

        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }
    return output;
}

std::vector<vec4> clipToViewFrustum(std::vector<vec4> polygon) {
    for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        polygon = clipAgainstPlane(polygon, plane);
    }
    return polygon;
}

float clipDistance(const Commonv2f& vertex, int plane) {
    return clipDistance(vertex.position, plane);
}

Commonv2f interpolateClipVertex(
    const Commonv2f& from,
    const Commonv2f& to,
    float t) {
    return {
        from.position + (to.position - from.position) * t,
        from.normal + (to.normal - from.normal) * t,
        from.texcoord + (to.texcoord - from.texcoord) * t,
    };
}

std::vector<Commonv2f> clipAgainstPlane(
    const std::vector<Commonv2f>& polygon,
    int plane) {
    std::vector<Commonv2f> output;
    if (polygon.empty()) {
        return output;
    }

    Commonv2f previous = polygon.back();
    float previousDistance = clipDistance(previous, plane);
    bool previousInside = previousDistance >= 0.0f;

    for (const Commonv2f& current : polygon) {
        const float currentDistance = clipDistance(current, plane);
        const bool currentInside = currentDistance >= 0.0f;

        if (currentInside != previousInside) {
            const float t = previousDistance /
                (previousDistance - currentDistance);
            output.push_back(interpolateClipVertex(previous, current, t));
        }
        if (currentInside) {
            output.push_back(current);
        }

        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }
    return output;
}

std::vector<Commonv2f> clipToViewFrustum(
    std::vector<Commonv2f> polygon) {
    for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        polygon = clipAgainstPlane(polygon, plane);
    }
    return polygon;
}

} // namespace

Renderer::Renderer(Framebuffer& framebuffer)
    : framebuffer_(framebuffer)
    , camera(framebuffer_.width(), framebuffer_.height()) {
    zbuffer_.resize(framebuffer_.width() * framebuffer_.height(), std::numeric_limits<float>::infinity());
}

void Renderer::clear(const Color& color)
{
    framebuffer_.clear(color);
    zbuffer_.assign(zbuffer_.size(), std::numeric_limits<float>::infinity());
}

float Renderer::getZbuffer(int x, int y)
{
    if (x < 0 || y < 0 || x >= framebuffer_.width() || y >= framebuffer_.height()) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(y) * framebuffer_.width() + x ;
    return zbuffer_[index];
}

void Renderer::setZbuffer(int x, int y, float zbuf)
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


void Renderer::drawTriangle_barycentric(int x0, int y0, float z0, int x1, int y1, float z1, int x2, int y2, float z2, const Color& color)
{
    int lb = std::max(0, std::min({ x0, x1, x2 }));
    int rb = std::min(framebuffer_.width() - 1, std::max({ x0, x1, x2 }));
    int bb = std::max(0, std::min({ y0, y1, y2 }));
    int tb = std::min(framebuffer_.height() - 1, std::max({ y0, y1, y2 }));

    if (lb > rb || bb > tb)
        return;
    float abc = area(x0, y0, x1, y1, x2, y2);
    if (abc < 1e-6f) return;
    for (int y = bb; y <= tb; y++)
    {
        for (int x = lb; x <= rb; x++)
        {
            float alpha = area(x, y, x1, y1, x2, y2)/ abc;
            float beta = area(x, y, x2, y2, x0, y0)/ abc;
            float gamma = area(x, y, x0, y0, x1, y1)/ abc;
            if (alpha < 0 || beta < 0 || gamma < 0)
                continue;
            float zbuf = alpha * z0 + beta * z1 + gamma *z2;
            float z_current = getZbuffer(x, y);
            if (zbuf < z_current)
            {
                setZbuffer(x, y, zbuf);
                //framebuffer_.setPixel(x, y, { static_cast < std::uint8_t>(color.r*alpha),  static_cast < std::uint8_t>(color.g *beta) ,  static_cast < std::uint8_t>(color.b*gamma), color.a});
                const float depthShade =
                    std::clamp(0.5f * (1.0f - zbuf), 0.0f, 1.0f);
                framebuffer_.setPixel(x, y, {
                            static_cast<std::uint8_t>(depthShade * color.r),
                            static_cast<std::uint8_t>(depthShade * color.g),
                            static_cast<std::uint8_t>(depthShade * color.b),
                            color.a
                            });
            }
        }
    }
}


void Renderer::drawTriangle_byShader(int x0, int y0, float z0, int x1, int y1, float z1, int x2, int y2, float z2, const CommonShader& shader, const Commonv2f (&vertices)[3])
{
    int lb = std::max(0, std::min({ x0, x1, x2 }));
    int rb = std::min(framebuffer_.width() - 1, std::max({ x0, x1, x2 }));
    int bb = std::max(0, std::min({ y0, y1, y2 }));
    int tb = std::min(framebuffer_.height() - 1, std::max({ y0, y1, y2 }));

    if (lb > rb || bb > tb)
        return;
    float abc = area(x0, y0, x1, y1, x2, y2);
    if (std::abs(abc) < 1e-6f) return;
    for (int y = bb; y <= tb; y++)
    {
        for (int x = lb; x <= rb; x++)
        {
            float alpha = area(x, y, x1, y1, x2, y2) / abc;
            float beta = area(x, y, x2, y2, x0, y0) / abc;
            float gamma = area(x, y, x0, y0, x1, y1) / abc;
            if (alpha < 0 || beta < 0 || gamma < 0)
                continue;
            float zbuf = alpha * z0 + beta * z1 + gamma * z2;
            float z_current = getZbuffer(x, y);
            if (zbuf < z_current)
            {
                const float correctedAlpha = alpha / vertices[0].position[3];
                const float correctedBeta = beta / vertices[1].position[3];
                const float correctedGamma = gamma / vertices[2].position[3];
                const float denominator =
                    correctedAlpha + correctedBeta + correctedGamma;
                if (std::abs(denominator) < 1e-8f) {
                    continue;
                }

                Commonv2f fragmentInput;
                fragmentInput.position =
                    (vertices[0].position * correctedAlpha +
                     vertices[1].position * correctedBeta +
                     vertices[2].position * correctedGamma) /
                    denominator;
                fragmentInput.normal = normalize(
                    (vertices[0].normal * correctedAlpha +
                     vertices[1].normal * correctedBeta +
                     vertices[2].normal * correctedGamma) /
                    denominator);
                fragmentInput.texcoord =
                    (vertices[0].texcoord * correctedAlpha +
                        vertices[1].texcoord * correctedBeta +
                        vertices[2].texcoord * correctedGamma) /
                    denominator;

				Color color{};
                if (shader.fragment(fragmentInput, color)) {
                    continue;
                }

                setZbuffer(x, y, zbuf);
                framebuffer_.setPixel(x, y, color);
            }
        }
    }
}

void Renderer::drawModel(const Model& model)
{
    const mat4 mvp = camera.projectionMatrix() * camera.viewTransformation();
    const float halfW = static_cast<float>(framebuffer_.width()) * 0.5f;
    const float halfH = static_cast<float>(framebuffer_.height()) * 0.5f;

    for (const auto& face : model.facet) {
        vec3 world[3] = {
            vec3(model.verts[face[0].positionIndex][0], model.verts[face[0].positionIndex][1], model.verts[face[0].positionIndex][2]),
            vec3(model.verts[face[1].positionIndex][0], model.verts[face[1].positionIndex][1], model.verts[face[1].positionIndex][2]),
            vec3(model.verts[face[2].positionIndex][0], model.verts[face[2].positionIndex][1], model.verts[face[2].positionIndex][2]),
        };

        std::vector<vec4> polygon;
        polygon.reserve(3);
        for (int i = 0; i < 3; ++i) {
            polygon.push_back(mvp * vec4(world[i], 1.0f));
        }

        polygon = clipToViewFrustum(std::move(polygon));
        if (polygon.size() < 3) {
            continue;
        }

        for (std::size_t triangle = 1; triangle + 1 < polygon.size(); ++triangle) {
            const vec4 clip[3] = {
                polygon[0], polygon[triangle], polygon[triangle + 1]
            };
            vec3 ndc[3];
            bool valid = true;

            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(clip[i][3]) ||
                    std::abs(clip[i][3]) < 1e-8f) {
                    valid = false;
                    break;
                }
                const vec4 divided = clip[i] / clip[i][3];
                if (!std::isfinite(divided[0]) ||
                    !std::isfinite(divided[1]) ||
                    !std::isfinite(divided[2])) {
                    valid = false;
                    break;
                }
                ndc[i] = vec3(divided);
            }
            if (!valid) {
                continue;
            }

            const int x0 = static_cast<int>((ndc[0][0] + 1.0f) * halfW);
            const int y0 = static_cast<int>((1.0f - ndc[0][1]) * halfH);
            const int x1 = static_cast<int>((ndc[1][0] + 1.0f) * halfW);
            const int y1 = static_cast<int>((1.0f - ndc[1][1]) * halfH);
            const int x2 = static_cast<int>((ndc[2][0] + 1.0f) * halfW);
            const int y2 = static_cast<int>((1.0f - ndc[2][1]) * halfH);

            drawTriangle_barycentric(
                x0, y0, ndc[0][2],
                x1, y1, ndc[1][2],
                x2, y2, ndc[2][2],
                {211, 211, 211, 255});
        }
    }
}

int Renderer::Pipeline(const Model& model, struct CommonShader& shader, RenderMod mod)
{
    shader.mvp = camera.projectionMatrix() * camera.viewTransformation();
	shader.vp = camera.viewTransformation();
	shader.SpecularLightDirection = SpecularLightDirection();
	shader.DiffuseLightDirection = DiffuseLightDirection();
	shader.AmbientLightColor = AmbientLightColor();
	shader.cameraPosition = camera.position_;
    const float halfW = static_cast<float>(framebuffer_.width()) * 0.5f;
    const float halfH = static_cast<float>(framebuffer_.height()) * 0.5f;
    int totalTriangles = 0;

    for (const auto& face : model.facet) {
        vec3 world[3] = {
            vec3(model.verts[face[0].positionIndex][0], model.verts[face[0].positionIndex][1], model.verts[face[0].positionIndex][2]),
            vec3(model.verts[face[1].positionIndex][0], model.verts[face[1].positionIndex][1], model.verts[face[1].positionIndex][2]),
            vec3(model.verts[face[2].positionIndex][0], model.verts[face[2].positionIndex][1], model.verts[face[2].positionIndex][2]),
        };

        vec3 faceNormal = cross(world[1] - world[0], world[2] - world[0]);
        const float faceNormalLength = norm(faceNormal);
        if (faceNormalLength < 1e-8f) {
            continue;
        }
        faceNormal /= faceNormalLength;

        vec3 worldNormals[3];
        for (int i = 0; i < 3; ++i) {
            const int normalIndex = face[i].normalIndex;
            if (normalIndex >= 0 &&
                static_cast<std::size_t>(normalIndex) < model.vnormals.size()) {
                worldNormals[i] = vec3(
                    model.vnormals[normalIndex][0],
                    model.vnormals[normalIndex][1],
                    model.vnormals[normalIndex][2]);
            } else {
                worldNormals[i] = faceNormal;
            }
        }

        vec2 texcoords[3];

        for (int i = 0; i < 3; ++i) {
            const int texcoordIndex = face[i].texcoordIndex;

            if (texcoordIndex >= 0 &&
                static_cast<std::size_t>(texcoordIndex) < model.vtexcoords.size()) {
                texcoords[i] = vec2(
                    model.vtexcoords[texcoordIndex][0],
                    model.vtexcoords[texcoordIndex][1]);
            }
            else {
                texcoords[i] = vec2(0.0f);
            }
        }

        std::vector<Commonv2f> polygon;
        polygon.reserve(3);
        for (int i = 0; i < 3; ++i) {
            polygon.push_back(shader.vertex(
                { world[i], worldNormals[i], texcoords[i] }));
        }

        polygon = clipToViewFrustum(std::move(polygon));
        if (polygon.size() < 3) {
            continue;
        }

        for (std::size_t triangle = 1; triangle + 1 < polygon.size(); ++triangle) {
            const Commonv2f clippedVertices[3] = {
                polygon[0], polygon[triangle], polygon[triangle + 1]
            };
            vec3 ndc[3];
            bool valid = true;

            for (int i = 0; i < 3; ++i) {
                const vec4& clipPosition = clippedVertices[i].position;
                if (!std::isfinite(clipPosition[3]) ||
                    std::abs(clipPosition[3]) < 1e-8f) {
                    valid = false;
                    break;
                }
                const vec4 divided = clipPosition / clipPosition[3];
                if (!std::isfinite(divided[0]) ||
                    !std::isfinite(divided[1]) ||
                    !std::isfinite(divided[2])) {
                    valid = false;
                    break;
                }
                ndc[i] = vec3(divided);
            }
            if (!valid) {
                continue;
            }
            const int x0 = static_cast<int>((ndc[0][0] + 1.0f) * halfW);
            const int y0 = static_cast<int>((1.0f - ndc[0][1]) * halfH);
            const int x1 = static_cast<int>((ndc[1][0] + 1.0f) * halfW);
            const int y1 = static_cast<int>((1.0f - ndc[1][1]) * halfH);
            const int x2 = static_cast<int>((ndc[2][0] + 1.0f) * halfW);
            const int y2 = static_cast<int>((1.0f - ndc[2][1]) * halfH);

            //drawTriangle_barycentric(
            //    x0, y0, ndc[0][2],
            //    x1, y1, ndc[1][2],
            //    x2, y2, ndc[2][2],
            //    { 211, 211, 211, 255 });
            switch (mod)
            {
                case RenderMod::DepthMap:
                {
                    const float depthShade =
                        std::clamp(0.5f * (1.0f - ndc[0][2]), 0.0f, 1.0f);
                    const Color depthColor = {
                        static_cast<std::uint8_t>(depthShade * 255),
                        static_cast<std::uint8_t>(depthShade * 255),
                        static_cast<std::uint8_t>(depthShade * 255),
                        255
                    };
                    drawTriangle_barycentric(
                        x0, y0, ndc[0][2],
                        x1, y1, ndc[1][2],
                        x2, y2, ndc[2][2],
                        depthColor);
				}
				case RenderMod::Filling:
                    drawTriangle_barycentric(
                        x0, y0, ndc[0][2],
                        x1, y1, ndc[1][2],
                        x2, y2, ndc[2][2],
                        { 211, 211, 211, 255 });
                    break;
                case RenderMod::Nolighting:break;
                case RenderMod::Lighting:
                    drawTriangle_byShader(
                        x0, y0, ndc[0][2],
                        x1, y1, ndc[1][2],
                        x2, y2, ndc[2][2],
                        shader,
                        clippedVertices); break;
				case RenderMod::Wireframe:
					drawLine(x0, y0, x1, y1, { 255, 255, 255, 255 });
                    drawLine(x1, y1, x2, y2, { 255, 255, 255, 255 });
                    drawLine(x2, y2, x0, y0, { 255, 255, 255, 255 });
					break;
            default:
                drawTriangle_byShader(
                    x0, y0, ndc[0][2],
                    x1, y1, ndc[1][2],
                    x2, y2, ndc[2][2],
                    shader,
                    clippedVertices);              
                break;
            }
            totalTriangles++;
        }    
    }
    return totalTriangles;
}

} // namespace jyd
