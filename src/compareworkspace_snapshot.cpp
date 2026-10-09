#include "compareworkspace_p.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QWidget>

namespace
{

QString snapshotDirectory()
{
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("compare/snapshotDir")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        return saved;
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty())
        return pictures;
    return QDir::homePath();
}

QString snapshotSuggestion()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString name = QStringLiteral("compare_") + stamp + QStringLiteral(".png");
    return QDir(snapshotDirectory()).filePath(name);
}

} // namespace

QPixmap CompareWorkspace::grabComparisonView()
{
    QWidget *target = nullptr;
    if (m_compareCanvas && m_compareCanvas->isVisible())
        target = m_compareCanvas;
    else
        target = m_compareGridPage;
    if (!target)
        target = this;
    return target->grab();
}

void CompareWorkspace::copyComparisonViewToClipboard()
{
    const QPixmap pm = grabComparisonView();
    if (pm.isNull())
        return;
    QApplication::clipboard()->setPixmap(pm);
    showCompareStatus(tr("已将当前对比视图复制到剪贴板"));
}

bool CompareWorkspace::saveComparisonViewTo(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QPixmap pm = grabComparisonView();
    if (pm.isNull())
        return false;
    return pm.save(path, "PNG");
}

void CompareWorkspace::saveComparisonViewToFile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("保存比较截图"),
                                                      snapshotSuggestion(), tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    const QString native = QDir::toNativeSeparators(path);
    if (!saveComparisonViewTo(path))
    {
        showCompareStatus(tr("保存截图失败：%1").arg(native));
        return;
    }
    QSettings().setValue(QStringLiteral("compare/snapshotDir"), QFileInfo(path).absolutePath());
    showCompareStatus(tr("已保存截图：%1").arg(native));
}

bool CompareWorkspace::handleClipboardCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool ctrl = (mods == Qt::ControlModifier);

    // Ctrl+Shift+C copies the focused image path. Exact Ctrl+C copies the view.
    if (key == Qt::Key_C && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        const QString path = focusImagePath();
        if (!path.isEmpty())
        {
            const QString native = QDir::toNativeSeparators(path);
            QApplication::clipboard()->setText(native);
            showCompareStatus(tr("已复制路径: %1").arg(native));
        }
        else
        {
            showCompareStatus(tr("没有可复制的路径"));
        }
        event->accept();
        return true;
    }
    if (ctrl && key == Qt::Key_C)
    {
        copyComparisonViewToClipboard();
        event->accept();
        return true;
    }
    if (ctrl && key == Qt::Key_S)
    {
        saveComparisonViewToFile();
        event->accept();
        return true;
    }
    return false;
}
