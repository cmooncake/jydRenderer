#include "framebuffer.hpp"
#include "renderer.hpp"
#include "window.hpp"
#include "model.hpp"
#include "model_selection_dialog.hpp"

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

int runRenderer(const jyd::Model& model) {
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
        while (!model) {
            jyd::ModelSelectionDialog dialog;
            if (dialog.exec() != QDialog::Accepted) {
                return 0;
            }

            const QByteArray utf8Path = dialog.selectedFile().toUtf8();
            try {
                model.emplace(std::filesystem::u8path(utf8Path.constData()));
            } catch (const std::exception& ex) {
                QMessageBox::critical(
                    nullptr,
                    QObject::tr("Cannot load model"),
                    QString::fromUtf8(ex.what()));
            }
        }

        return runRenderer(*model);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        QMessageBox::critical(
            nullptr,
            QObject::tr("Renderer error"),
            QString::fromUtf8(ex.what()));
        return 1;
    }
}
