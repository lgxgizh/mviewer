#include "widgets/pythonscriptdialog.h"

#include "application/PythonScriptRunner.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
const char *kProtocol = R"(
Protocol (no shell, no in-process import):
  the runner calls   <interpreter> <script> <request.json> <response.json>
  request  = {"image": "<absolute path>", "args": {...}}
  response = {"ok": true, "message": "...", "boxes": [[x, y, w, h, "label"], ...]}
           or {"ok": false, "message": "why"}
The script has 30 seconds and inherits the environment unchanged.
)";
}

PythonScriptDialog::PythonScriptDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Run Python Script"));
    resize(640, 420);
    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    m_scripts = new QComboBox(this);
    m_scripts->setEditable(true);
    m_scripts->setMinimumWidth(320);
    form->addRow(tr("Script"), m_scripts);

    m_imageLabel = new QLabel(this);
    form->addRow(tr("Image"), m_imageLabel);

    auto *choose = new QPushButton(tr("Browse…"), this);
    form->addRow(QString(), choose);
    layout->addLayout(form);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setPlaceholderText(QString::fromUtf8(kProtocol));
    layout->addWidget(m_output, 1);

    auto *buttons = new QHBoxLayout();
    m_run = new QPushButton(tr("Run"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_run);
    layout->addLayout(buttons);

    connect(choose, &QPushButton::clicked, this, &PythonScriptDialog::chooseScript);
    connect(m_run, &QPushButton::clicked, this, &PythonScriptDialog::runScript);

    appendResult(
        tr("Interpreter: %1")
            .arg(mviewer::application::PythonScriptRunner::instance().detectInterpreter().isEmpty()
                     ? tr("not found — set MVIEWER_PYTHON")
                     : mviewer::application::PythonScriptRunner::instance().detectInterpreter()));
    refreshRunState();
}

void PythonScriptDialog::setImagePath(const QString &path)
{
    m_imagePath = path;
    m_imageLabel->setText(path.isEmpty() ? tr("(no image)") : QFileInfo(path).fileName());
    refreshRunState();
}

QString PythonScriptDialog::imagePath() const
{
    return m_imagePath;
}

void PythonScriptDialog::refreshRunState()
{
    m_run->setEnabled(
        !m_imagePath.isEmpty() &&
        mviewer::application::PythonScriptRunner::isScriptPath(m_scripts->currentText()));
}

void PythonScriptDialog::chooseScript()
{
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Choose a Python script"), m_scripts->currentText(),
                                     tr("Python (*.py);;All files (*)"));
    if (path.isEmpty())
        return;
    m_scripts->setCurrentText(path);
    refreshRunState();
}

void PythonScriptDialog::appendResult(const QString &text)
{
    m_output->appendPlainText(text);
}

void PythonScriptDialog::runScript()
{
    const QString script = m_scripts->currentText();
    if (!mviewer::application::PythonScriptRunner::isScriptPath(script))
    {
        appendResult(tr("Choose a .py script first."));
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("image"), m_imagePath);
    request.insert(QStringLiteral("args"), QJsonObject());

    appendResult(tr("Running %1 on %2…")
                     .arg(QFileInfo(script).fileName(), QFileInfo(m_imagePath).fileName()));
    QString error;
    const auto result =
        mviewer::application::PythonScriptRunner::instance().run(script, request, error);
    if (!error.isEmpty())
    {
        appendResult(tr("Failed: %1").arg(error));
        return;
    }
    appendResult(result.ok ? tr("ok: %1").arg(result.message)
                           : tr("script reported failure: %1").arg(result.message));
    for (const QString &box : result.boxes)
        appendResult(tr("  box %1").arg(box));
}
