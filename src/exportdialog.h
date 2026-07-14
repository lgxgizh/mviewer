#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>

// ExportDialog：单图/批量导出对话框
class ExportDialog : public QDialog
{
Q_OBJECT

public:
    explicit ExportDialog(const QStringList& sourceImages, QWidget* parent = nullptr);

private:
    void setupUi();
    void onBrowseOutput();
    void onFormatChanged(int index);
    void onExportClicked();
    void exportSingle(const QString& src, const QString& dst);
    void exportBatch();

    QStringList m_sourceImages;
    QLineEdit* m_outputEdit = nullptr;
    QComboBox* m_formatCombo = nullptr;
    QSpinBox* m_qualitySpin = nullptr;
    QCheckBox* m_batchCheck = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_exportBtn = nullptr;
};
