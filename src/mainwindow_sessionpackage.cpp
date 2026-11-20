// Portable session package export/import (report feature 11).
//
// The package is a store-only ZIP: a session.json manifest (the serialized
// compare session plus the list of bundled files) and the referenced images.
// Import extracts to a temporary directory and reopens the session from there,
// so the original files are never modified.
#include "mainwindow.h"

#include "compareworkspace.h"
#include "core/session/SessionPackage.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>

#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> bytesOf(const QByteArray &data)
{
    const char *raw = data.constData();
    return std::vector<std::uint8_t>(raw, raw + data.size());
}

std::vector<std::uint8_t> bytesOf(const QString &text)
{
    return bytesOf(text.toUtf8());
}
} // namespace

void MainWindow::exportSessionPackage()
{
    if (!m_compareView)
    {
        statusBar()->showMessage(tr("Session package: open a compare session first"), 5000);
        return;
    }
    const QString destination = QFileDialog::getSaveFileName(
        this, tr("Export Session Package"), QString(), tr("MViewer Session (*.mviewer-session)"));
    if (destination.isEmpty())
        return;

    const QStringList images = m_compareView->comparedImages();
    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    const auto cs = m_compareView->compareSession();
    manifest.insert(QStringLiteral("compareSession"),
                    QString::fromStdString(mviewer::core::serializeCompareSession(cs)));

    std::vector<mviewer::core::SessionPackageEntry> entries;
    QJsonArray files;
    for (int i = 0; i < images.size(); ++i)
    {
        QFile file(images.at(i));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = file.readAll();
        const QString source = images.at(i);
        const QString stem = QFileInfo(source).completeBaseName();
        const std::string name = "images/" + std::to_string(i) + "-" + stem.toStdString() + ".bin";
        entries.push_back({name, bytesOf(data)});
        files.append(QString::fromStdString(name));
    }
    manifest.insert(QStringLiteral("files"), files);
    entries.insert(entries.begin(), {"session.json", bytesOf(QJsonDocument(manifest).toJson())});

    std::string error;
    if (!mviewer::core::writeSessionPackage(destination.toUtf8().toStdString(), entries, error))
    {
        QMessageBox::warning(this, tr("Session Package"),
                             tr("Could not write the package") + QStringLiteral(": ") +
                                 QString::fromStdString(error));
        return;
    }
    statusBar()->showMessage(
        tr("Session package: %1 (%2 files)").arg(destination).arg(entries.size()), 6000);
}

void MainWindow::importSessionPackage()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("Import Session Package"), QString(), tr("MViewer Session (*.mviewer-session)"));
    if (source.isEmpty())
        return;

    std::string error;
    const auto entries = mviewer::core::readSessionPackage(source.toUtf8().toStdString(), error);
    if (entries.empty())
    {
        QMessageBox::warning(this, tr("Session Package"),
                             tr("Could not read the package") + QStringLiteral(": ") +
                                 QString::fromStdString(error));
        return;
    }

    QTemporaryDir tempDir(QDir::tempPath() + QStringLiteral("/mviewer-session-XXXXXX"));
    if (!tempDir.isValid())
    {
        QMessageBox::warning(this, tr("Session Package"), tr("No temporary folder available"));
        return;
    }

    QString manifestJson;
    QStringList imagePaths;
    for (const auto &entry : entries)
    {
        const QString relative = QString::fromStdString(entry.name);
        const QString target = tempDir.filePath(relative);
        if (relative.contains(QLatin1Char('/')))
            QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, tr("Session Package"),
                                 tr("Could not extract %1").arg(relative));
            return;
        }
        out.write(reinterpret_cast<const char *>(entry.data.data()),
                  static_cast<qint64>(entry.data.size()));
        out.close();
        if (relative == QLatin1String("session.json"))
            manifestJson = QString::fromUtf8(reinterpret_cast<const char *>(entry.data.data()),
                                             static_cast<int>(entry.data.size()));
        else if (relative.startsWith(QLatin1String("images/")))
            imagePaths.append(target);
    }

    if (manifestJson.isEmpty() || imagePaths.isEmpty())
    {
        QMessageBox::warning(this, tr("Session Package"),
                             tr("The package has no session manifest or no images"));
        return;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestJson.toUtf8()).object();
    openCompare(imagePaths, manifest.value(QStringLiteral("compareSession")).toString());
}
