#pragma once

#include "framebuffer.hpp"
#include "camera.hpp"
#include "model.hpp"

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
	};

    struct Commonv2f {
        vec4 position;
        vec3 normal;
	};

    struct CommonShader : public IShader<Commona2v, Commonv2f> {
        mat4 mvp;
		mat4 vp;
        Commonv2f vertex(const Commona2v& vertex) const override {
            vec4 pos =  mvp * vec4(vertex.position, 1.0);
            vec4 n = normalize(vp * vec4(vertex.normal, 0.0));
			
			return { pos, vec3(n) };   
        }
        bool fragment(const Commonv2f& f, Color& color) const override {
            const double intensity = std::clamp(f.normal[2], 0.0, 1.0);
            color = { static_cast<std::uint8_t>(intensity * 211),
                      static_cast<std::uint8_t>(intensity * 211),
                      static_cast<std::uint8_t>(intensity * 74),
                      255 };
            return false;
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
    void drawTriangle_byShader(int x0, int y0, double z0, int x1, int y1, double z1, int x2, int y2, double z2, const CommonShader& shader, const Commonv2f (&vertices)[3]);
    void drawModel(const Model& model);

	void Pipeline(const Model& model, struct CommonShader& shader);

private:
    Framebuffer& framebuffer_;
    std::vector<double> zbuffer_;
    Camera camera;
};

} // namespace jyd
