#include "framebuffer.hpp"
#include "renderer.hpp"
#include "window.hpp"
#include "model.hpp"
#include "model_selection_dialog.hpp"
#include "texture.hpp"
#if defined(JYD_ENABLE_LLVM)
#include "llvm_shader.hpp"
#endif

#include <QApplication>
#include <QByteArray>
#include <QDialog>
#include <QMessageBox>

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

jyd::RenderMod mod = jyd::RenderMod::Lighting;

namespace {

struct RenderItem {
    jyd::Model model;
    jyd::Texture texture;

    RenderItem(
        const std::filesystem::path& modelPath,
        const std::filesystem::path& texturePath)
        : model(modelPath), texture(texturePath) {}
};

int runRenderer(
    const std::vector<RenderItem>& scene,
    jyd::RenderMod mod,
    jyd::ShaderProgram shaderProgram,
    const std::filesystem::path& shaderFile) {
    constexpr int kWidth = 1200;
    constexpr int kHeight = 900;

    jyd::Window window("jydRenderer", kWidth, kHeight);
    jyd::Framebuffer framebuffer(kWidth, kHeight);
    jyd::Renderer renderer(framebuffer);

    bool init = false;

    //using Clock = std::chrono::steady_clock;
    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    double fps = 0.0;
    int totalTriangles = 0;

    std::unique_ptr<jyd::CommonShader> shader;
#if defined(JYD_ENABLE_LLVM)
    if (shaderProgram == jyd::ShaderProgram::LlvmCommon) {
        shader = shaderFile.empty()
            ? std::make_unique<jyd::LlvmCommonShader>()
            : std::make_unique<jyd::LlvmCommonShader>(shaderFile);
    }
#else
    (void)shaderProgram;
    (void)shaderFile;
#endif
    if (!shader) {
        shader = std::make_unique<jyd::NativeCommonShader>();
    }
    std::cout << "Shader: " << shader->name() << '\n';

    while (true) {
        auto frame = window.pollEvents(renderer, mod);
        if (!frame.running) break;
        if (!init || frame.needsRedraw) {
            renderer.clear({ 20, 24, 33, 255 });

            totalTriangles = 0;
            for (const RenderItem& item : scene) {
                shader->texture = &item.texture;
                totalTriangles += renderer.Pipeline(
                    item.model, *shader, mod);
            }
            window.present(framebuffer);
            init = true;
        }
        ++frameCount;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        if (elapsed >= 1.0) {
            fps = frameCount / elapsed;
            frameCount = 0;
            lastTime = now;
        }
        //std::this_thread::sleep_for(std::chrono::milliseconds(16));
        std::cout << "\033[2J\033[H";
        std::cout << "Triangles: " << totalTriangles
            << "  FPS: " << static_cast<int>(fps) << std::endl;
       
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    
    try {
        std::vector<RenderItem> scene;
        jyd::ShaderProgram shaderProgram =
            jyd::ShaderProgram::NativeCommon;
        std::filesystem::path shaderFile;
        while (true) {
            jyd::ModelSelectionDialog dialog;
            if (dialog.exec() != QDialog::Accepted) {
                return 0;
            }

            try {
                std::vector<RenderItem> loadedScene;
                loadedScene.reserve(dialog.selections().size());

                for (const jyd::ModelTextureSelection& selection :
                     dialog.selections()) {
                    const QByteArray modelPath =
                        selection.modelFile.toUtf8();
                    const QByteArray texturePath =
                        selection.textureFile.toUtf8();

                    loadedScene.emplace_back(
                        std::filesystem::u8path(modelPath.constData()),
                        std::filesystem::u8path(texturePath.constData()));
                }

                scene = std::move(loadedScene);
                shaderProgram = dialog.shaderProgram();
                const QByteArray shaderPath = dialog.shaderFile().toUtf8();
                shaderFile = shaderPath.isEmpty()
                    ? std::filesystem::path{}
                    : std::filesystem::u8path(shaderPath.constData());
                break;
            } catch (const std::exception& ex) {
                QMessageBox::critical(
                    nullptr,
                    QObject::tr("Cannot load resources"),
                    QString::fromUtf8(ex.what()));
            }
        }

        std::cout << "Loaded " << scene.size()
                  << " model/texture pair(s).\n";
        return runRenderer(scene, mod, shaderProgram, shaderFile);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        QMessageBox::critical(
            nullptr,
            QObject::tr("Renderer error"),
            QString::fromUtf8(ex.what()));
        return 1;
    }
}
