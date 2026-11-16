// PreviewPanel Decode submission and rejected-submit recovery.
// The scheduler stays frozen: a refused submit is still nullptr. This TU
// retries that one preview a finite number of times, then either keeps a
// sharp preview already on screen or paints a terminal failure.
#include "previewpanel.h"

#include <QTimer>

void PreviewPanel::stopSubmitRetry()
{
    m_submitRetryArmed = false;
    m_submitRetriesLeft = 0;
    if (m_submitRetry)
        m_submitRetry->stop();
}

bool PreviewPanel::previewSubmitRetryPendingForTest() const
{
    return m_submitRetryArmed || (m_submitRetry && m_submitRetry->isActive());
}

QString PreviewPanel::submitFailureText() const
{
    // Pixels stay in charge. The sentence is only for an empty well.
    if (!m_previewSubmitFailed || m_hasImage)
        return {};
    return tr("Unable to load preview");
}

void PreviewPanel::finishSubmitRejected(uint64_t gen)
{
    stopSubmitRetry();
    if (gen != m_requestGen || !m_lifetime->isAlive())
        return;
    // A refresh that the scheduler will not take does not demote a preview
    // this panel already showed for the same path.
    if (m_hasImage && m_presentedPath == m_requestedPath &&
        m_quality == PresentationQuality::Preview)
        return;
    m_previewSubmitFailed = true;
    update();
}

void PreviewPanel::armSubmitRetryOrFail(uint64_t gen)
{
    if (gen != m_requestGen || !m_lifetime->isAlive() || m_requestedPath.isEmpty())
        return;
    if (m_submitRetriesLeft <= 0)
    {
        finishSubmitRejected(gen);
        return;
    }
    --m_submitRetriesLeft;
    m_submitRetryGen = gen;
    if (!m_submitRetry)
    {
        m_submitRetry = new QTimer(this);
        m_submitRetry->setSingleShot(true);
        connect(m_submitRetry, &QTimer::timeout, this, &PreviewPanel::onSubmitRetry);
    }
    m_submitRetryArmed = true;
    m_submitRetry->start(kSubmitRetryDelayMs);
}

void PreviewPanel::onSubmitRetry()
{
    // stop() clears the armed flag before the generation changes. A timeout
    // that was already queued must not submit, and must not touch a newer
    // generation that has armed the same timer again.
    if (!m_submitRetryArmed || m_submitRetryGen != m_requestGen)
        return;
    m_submitRetryArmed = false;
    if (!m_lifetime->isAlive() || m_requestedPath.isEmpty() || m_task)
        return;
    submitCurrentPreview();
}

void PreviewPanel::submitCurrentPreview()
{
    if (m_task || m_requestedPath.isEmpty() || !m_lifetime->isAlive())
        return;
    const uint64_t gen = m_requestGen;
    const QString path = m_requestedPath;
    const int knownW = m_submitKnownW;
    const int knownH = m_submitKnownH;
    const qint64 knownSize = m_submitKnownSize;
    ++m_submitAttempts;

    QPointer<PreviewPanel> guard(this);
    const auto lifetime = m_lifetime;
    const std::string stdPath = path.toUtf8().toStdString();
    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Decode,
        [stdPath, path, gen, guard, lifetime, knownW, knownH,
         knownSize](const TaskScheduler::TaskContext &ctx)
        {
            decodePreviewWorker(stdPath, path, gen, guard, lifetime, knownW, knownH, knownSize,
                                ctx);
        });
    if (handle)
    {
        m_task = handle;
        stopSubmitRetry();
        return;
    }
    armSubmitRetryOrFail(gen);
}
