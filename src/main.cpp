#include "framebuffer.hpp"
#include "renderer.hpp"
#include "window.hpp"
#include "model.hpp"
#include "model_selection_dialog.hpp"
#include "texture.hpp"

#include <QApplication>
#include <QByteArray>
#include <QDialog>
#include <QMessageBox>

#include <filesystem>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

namespace {

struct RenderItem {
    jyd::Model model;
    jyd::Texture texture;

    RenderItem(
        const std::filesystem::path& modelPath,
        const std::filesystem::path& texturePath)
        : model(modelPath), texture(texturePath) {}
};

int runRenderer(const std::vector<RenderItem>& scene) {
    constexpr int kWidth = 1200;
    constexpr int kHeight = 900;

    jyd::Window window("jydRenderer", kWidth, kHeight);
    jyd::Framebuffer framebuffer(kWidth, kHeight);
    jyd::Renderer renderer(framebuffer);

    bool init = false;

    while (true) {
        auto frame = window.pollEvents(renderer);
        if (!frame.running) break;
        if (!init || frame.needsRedraw) {
            renderer.clear({ 20, 24, 33, 255 });

            jyd::CommonShader shader;
            for (const RenderItem& item : scene) {
                shader.texture = &item.texture;
                renderer.Pipeline(item.model, shader);
            }
            window.present(framebuffer);
            init = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    try {
        std::vector<RenderItem> scene;
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
        return runRenderer(scene);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        QMessageBox::critical(
            nullptr,
            QObject::tr("Renderer error"),
            QString::fromUtf8(ex.what()));
        return 1;
    }
}
