#pragma once

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

// Parameter rows for BatchDialog. Kept out of batchdialog.cpp so that file
// stays within the 800-line TU cap.
namespace batchdialog_detail
{

QWidget *makeResizePanel(QSpinBox *&maxEdge);
QWidget *makeCropPanel(QSpinBox *&x, QSpinBox *&y, QSpinBox *&w, QSpinBox *&h);
QWidget *makeWatermarkPanel(QLineEdit *&text, QComboBox *&pos, QDoubleSpinBox *&opacity,
                            QSpinBox *&fontSize);
QWidget *makeExportPanel(QComboBox *&format, QSpinBox *&quality, QLineEdit *&outputDir,
                         QPushButton *&browseBtn, QCheckBox *&overwriteExisting);

} // namespace batchdialog_detail
