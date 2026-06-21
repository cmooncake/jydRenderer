#include "window.hpp"

#if defined(JYD_USE_SDL2)
#include <SDL.h>
#endif

#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace jyd {

Window::Window(const char* title, int width, int height) : width_(width), height_(height) {
#if defined(JYD_USE_SDL2)
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(SDL_GetError());
    }

    auto* window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_SHOWN);

    if (window == nullptr) {
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }

    auto* sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (sdlRenderer == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }

    auto* texture = SDL_CreateTexture(
        sdlRenderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);

    if (texture == nullptr) {
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }

    handle_ = window;
    surface_ = sdlRenderer;
    texture_ = texture;
#else
    (void)title;
    throw std::runtime_error("No window backend enabled. Build with JYD_USE_SDL2=ON.");
#endif
}

Window::~Window() {
#if defined(JYD_USE_SDL2)
    if (texture_ != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(texture_));
    }
    if (surface_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(surface_));
    }
    if (handle_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(handle_));
    }
    SDL_Quit();
#endif
}

bool Window::pollEvents(Renderer& renderer) {
#if defined(JYD_USE_SDL2)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return false;

            case SDL_KEYDOWN: {
                Camera& camera = renderer.getCamera();
                switch (event.key.keysym.scancode) {
                case SDL_SCANCODE_W: camera.moveForward(0.1); break;
                case SDL_SCANCODE_S: camera.moveBackward(0.1); break;
                case SDL_SCANCODE_A: camera.strafeLeft(0.1); break;
                case SDL_SCANCODE_D: camera.strafeRight(0.1); break;
                case SDL_SCANCODE_Q: camera.moveUp(0.1); break;
                case SDL_SCANCODE_E: camera.moveDown(0.1); break;
                default: break;
                }
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    dragging_ = true;
                    lastMouseX_ = event.button.x;
                    lastMouseY_ = event.button.y;
                }
                break;

            case SDL_MOUSEMOTION:
                if (dragging_ || (event.motion.state & SDL_BUTTON_RMASK)) {
                    const int deltaX = event.motion.x - lastMouseX_;
                    const int deltaY = event.motion.y - lastMouseY_;
                    if (deltaX != 0 || deltaY != 0) {
                        renderer.getCamera().rotate(deltaX, deltaY);
                    }
                    lastMouseX_ = event.motion.x;
                    lastMouseY_ = event.motion.y;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    dragging_ = false;
                }
                break;

            default:
                break;
        }
    }
    return true;
#else
    (void)renderer;
    return false;
#endif
}

void Window::present(const Framebuffer& framebuffer) {
#if defined(JYD_USE_SDL2)
    auto* window = static_cast<SDL_Window*>(handle_);
    auto* sdlRenderer = static_cast<SDL_Renderer*>(surface_);
    auto* texture = static_cast<SDL_Texture*>(texture_);

    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(
            sdlRenderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            width_,
            height_);
        if (texture == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        texture_ = texture;
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) != 0) {
        throw std::runtime_error(SDL_GetError());
    }

    const std::uint8_t* src = framebuffer.data();
    auto* dst = static_cast<std::uint8_t*>(pixels);
    const int copyWidth = std::min(width_, framebuffer.width());
    const int copyHeight = std::min(height_, framebuffer.height());

    for (int y = 0; y < copyHeight; ++y) {
        const std::size_t srcRow = static_cast<std::size_t>(y) * framebuffer.width() * 4;
        std::memcpy(dst + y * pitch, src + srcRow, static_cast<std::size_t>(copyWidth) * 4);
    }

    SDL_UnlockTexture(texture);

    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, texture, nullptr, nullptr);
    SDL_RenderPresent(sdlRenderer);
#else
    (void)framebuffer;
#endif
}

} // namespace jyd
