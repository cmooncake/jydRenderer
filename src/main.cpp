#include "framebuffer.hpp"
#include "renderer.hpp"
#include "window.hpp"
#include "model.hpp"

#include <iostream>
#include <thread>
#include <chrono>

int main() {
    constexpr int kWidth = 1200;
    constexpr int kHeight = 900;

    try {
        jyd::Window window("jydRenderer", kWidth, kHeight);
        jyd::Framebuffer framebuffer(kWidth, kHeight);
        jyd::Renderer renderer(framebuffer);
        std::string file = "C:/git_proj/tinyrenderer/obj/diablo3_pose/diablo3_pose.obj";
        jyd::Model head(file);

        bool init = false;

        //renderer.drawTriangle(100, 100, 200, 100, 150, 200, {255, 0, 0, 255}); 
        while (true) {
			auto frame = window.pollEvents(renderer);
			if (!frame.running) break;
            if (!init || frame.needsRedraw) {
                renderer.clear({ 20, 24, 33, 255 });

                //renderer.drawTriangle(100, 100, 200, 100, 150, 200, {255, 0, 0, 255});
                jyd::CommonShader shader;
				renderer.Pipeline(head, shader);
                //renderer.drawModel(head);
                window.present(framebuffer);
				if (!init) init = true;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
