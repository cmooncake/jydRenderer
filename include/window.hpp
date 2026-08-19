#pragma once

#include "framebuffer.hpp"
#include "renderer.hpp"

namespace jyd {

struct PollResult {
     bool running;
     bool needsRedraw;
     PollResult() : running(true), needsRedraw(false) {}
     PollResult(bool r, bool d) : running(r), needsRedraw(d) {}
     PollResult(const PollResult & instance) : running(instance.running), needsRedraw(instance.needsRedraw) {}
};

class Window {
public:
    Window(const char* title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    struct PollResult pollEvents(Renderer& renderer, jyd::RenderMod& mod);
    void present(const Framebuffer& framebuffer);

private:
    int width_;
    int height_;
    void* handle_ = nullptr;
    void* surface_ = nullptr;
    void* texture_ = nullptr;
    bool dragging_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;

};

} // namespace jyd
