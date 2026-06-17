#pragma once

#include "framebuffer.hpp"
#include "renderer.hpp"

namespace jyd {

class Window {
public:
    Window(const char* title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool pollEvents(Renderer& renderer);
    void present(const Framebuffer& framebuffer);

private:
    int width_;
    int height_;
    void* handle_ = nullptr;
    void* surface_ = nullptr;
    void* texture_ = nullptr;
};

} // namespace jyd
