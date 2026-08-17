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
#include <optional>
#include <thread>
#include <chrono>

namespace {

int runRenderer(const jyd::Model& model, const jyd::Texture& texture) {
    constexpr int kWidth = 1900;
    constexpr int kHeight = 1200;

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
            shader.texture = &texture;

            renderer.Pipeline(model, shader);
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
        std::optional<jyd::Model> model;
        std::optional<jyd::Texture> texture;
        while (!model || !texture) {
            jyd::ModelSelectionDialog dialog;
            if (dialog.exec() != QDialog::Accepted) {
                return 0;
            }

            const QByteArray modelPath =
                dialog.selectedFile().toUtf8();

            const QByteArray texturePath =
                dialog.selectedTextureFile().toUtf8();
            try {
                model.emplace(std::filesystem::u8path(
                    modelPath.constData()));

                texture.emplace(std::filesystem::u8path(
                    texturePath.constData()));
            } catch (const std::exception& ex) {
                model.reset();
                texture.reset();
                QMessageBox::critical(
                    nullptr,
                    QObject::tr("Cannot load resources"),
                    QString::fromUtf8(ex.what()));
            }
        }
        std::cout
            << "Texture loaded: "
            << texture->width()
            << " x "
            << texture->height()
            << '\n';
        return runRenderer(*model, *texture);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        QMessageBox::critical(
            nullptr,
            QObject::tr("Renderer error"),
            QString::fromUtf8(ex.what()));
        return 1;
    }
}
