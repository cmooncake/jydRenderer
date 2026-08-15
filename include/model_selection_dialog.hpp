#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

namespace jyd {

class ModelSelectionDialog final : public QDialog {
public:
    explicit ModelSelectionDialog(QWidget* parent = nullptr);

    QString selectedFile() const;

protected:
    void accept() override;

private:
    void browse();
    void updateConfirmState();

    QLineEdit* pathEdit_ = nullptr;
    QPushButton* confirmButton_ = nullptr;
};

} // namespace jyd
