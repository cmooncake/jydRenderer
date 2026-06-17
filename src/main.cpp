#include "framebuffer.hpp"
#include "renderer.hpp"
#include "window.hpp"
#include "model.hpp"

#include <iostream>
#include <thread>
#include <chrono>

int main() {
    constexpr int kWidth = 800;
    constexpr int kHeight = 600;

    try {
        jyd::Window window("jydRenderer", kWidth, kHeight);
        jyd::Framebuffer framebuffer(kWidth, kHeight);
        jyd::Renderer renderer(framebuffer);
        std::string file = "C:/Users/jyd/Downloads/african_head.obj";
        jyd::Model head(file);

        while (window.pollEvents(renderer)) {
            renderer.clear({20, 24, 33, 255});

            renderer.drawModel(head);
            window.present(framebuffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
