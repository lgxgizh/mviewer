#include "analysispanel.h"
#include "analyzermodel.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/compare/Aligner.h"
#include "widgets/rawimageview.h"
#include <QSettings>

#include "core/image/QtConvert.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

// ── M21: AnalyzerModel wiring (history / pin / result SSOT) ──

void AnalysisPanel::setAnalyzerModel(AnalyzerModel *model)
{
    if (m_analyzerModel == model)
        return;
    if (m_analyzerModel)
        disconnect(m_analyzerModel, nullptr, this, nullptr);
    m_analyzerModel = model;
    if (!m_analyzerModel)
        return;
    connect(m_analyzerModel, &AnalyzerModel::historyChanged, this,
            [this](const QStringList &) { refreshHistoryUi(); });
    connect(m_analyzerModel, &AnalyzerModel::pinnedChanged, this,
            [this](const QStringList &) { refreshPinnedUi(); });
    refreshHistoryUi();
    refreshPinnedUi();
}

void AnalysisPanel::publishResult(const QString &plainText)
{
    if (!m_analyzerModel || plainText.isEmpty())
        return;
    const QString path =
        !m_imagePath.isEmpty()
            ? m_imagePath
            : (m_frameA ? QString::fromStdString(m_frameA->metadata().filePath) : QString());
    if (path.isEmpty())
        return;
    m_analyzerModel->setResult(path, plainText);
    if (m_analyzerCombo)
    {
        const QString id = m_analyzerCombo->currentData().toString();
        if (!id.isEmpty())
            m_analyzerModel->setCurrentAnalyzer(id);
    }
    if (m_pinBtn)
        m_pinBtn->setChecked(m_analyzerModel->isPinned(path));
}

void AnalysisPanel::refreshHistoryUi()
{
    if (!m_historyList || !m_analyzerModel)
        return;
    m_historyList->clear();
    for (const QString &p : m_analyzerModel->history())
    {
        auto *item = new QListWidgetItem(QFileInfo(p).fileName(), m_historyList);
        item->setData(Qt::UserRole, p);
        item->setToolTip(p);
    }
}

void AnalysisPanel::refreshPinnedUi()
{
    if (!m_pinnedList || !m_analyzerModel)
        return;
    m_pinnedList->clear();
    for (const QString &p : m_analyzerModel->pinned())
    {
        auto *item = new QListWidgetItem(QFileInfo(p).fileName(), m_pinnedList);
        item->setData(Qt::UserRole, p);
        item->setToolTip(p);
    }
}

void AnalysisPanel::onHistoryActivated()
{
    if (!m_historyList)
        return;
    auto *item = m_historyList->currentItem();
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    if (m_analyzerModel)
    {
        const QString text = m_analyzerModel->resultText(path);
        if (!text.isEmpty())
            setRegionStats(text);
    }
    emit historyImageRequested(path);
}

void AnalysisPanel::onPinnedActivated()
{
    if (!m_pinnedList)
        return;
    auto *item = m_pinnedList->currentItem();
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    if (m_analyzerModel)
    {
        const QString text = m_analyzerModel->resultText(path);
        if (!text.isEmpty())
            setRegionStats(text);
    }
    emit historyImageRequested(path);
}

void AnalysisPanel::onPinToggled()
{
    if (!m_analyzerModel || !m_pinBtn)
        return;
    const QString path =
        !m_imagePath.isEmpty()
            ? m_imagePath
            : (m_frameA ? QString::fromStdString(m_frameA->metadata().filePath) : QString());
    if (path.isEmpty())
    {
        m_pinBtn->setChecked(false);
        return;
    }
    // Ensure there is a result to pin (use current plugin text if needed).
    if (m_analyzerModel->resultText(path).isEmpty() && m_pluginResult &&
        !m_pluginResult->text().isEmpty())
        m_analyzerModel->setResult(path, m_pluginResult->text());
    if (m_pinBtn->isChecked())
        m_analyzerModel->pinResult(path);
    else
        m_analyzerModel->unpinResult(path);
}
