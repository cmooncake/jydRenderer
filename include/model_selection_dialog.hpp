#pragma once

#include <QDialog>
#include <QString>
#include <vector>

class QLineEdit;
class QListWidget;
class QPushButton;
class QComboBox;

namespace jyd {

struct ModelTextureSelection {
    QString modelFile;
    QString textureFile;
};

enum class ShaderProgram {
    NativeCommon = 0,
    LlvmCommon = 1
};

class ModelSelectionDialog final : public QDialog {
public:
    explicit ModelSelectionDialog(QWidget* parent = nullptr);

    const std::vector<ModelTextureSelection>& selections() const;
    ShaderProgram shaderProgram() const;
    QString shaderFile() const;

protected:
    void accept() override;

private:
    void browse();
    void browseTexture();
    void browseShader();
    void addSelection();
    void removeSelected();
    bool currentSelectionValid() const;
    void updateConfirmState();

    QLineEdit* pathEdit_ = nullptr;
    QLineEdit* texturePathEdit_ = nullptr;
    QListWidget* selectionList_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* confirmButton_ = nullptr;
    QComboBox* shaderProgramCombo_ = nullptr;
    std::vector<ModelTextureSelection> selections_;
};

} // namespace jyd
