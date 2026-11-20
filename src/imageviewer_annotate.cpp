// Review markup for ImageViewer: rectangles, arrows and text labels drawn in
// image coordinates, persisted next to the source, and burned into an exported
// review image.
#include "imageviewer.h"

#include "core/image/QtConvert.h"
#include "widgets/blindtestdialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>

void ImageViewer::setAnnotateMode(bool on)
{
    if (m_annotateMode == on)
        return;
    m_annotateMode = on;
    if (on)
    {
        ensureAnnotationsForCurrentPath();
        setProfileMode(false); // one drawing tool at a time
    }
    if (!on)
        m_annotating = false;
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    update();
}

bool ImageViewer::isAnnotateMode() const
{
    return m_annotateMode;
}

void ImageViewer::addReviewContextActions(QMenu &menu, QAction *&annotate, QAction *&textLabel,
                                          QAction *&exportReview, QAction *&colorChecker,
                                          QAction *&blindTest, QAction *&spectrum,
                                          QAction *&cvBoxes, bool hasView)
{
    annotate = menu.addAction(tr("Annotate (A)"));
    annotate->setCheckable(true);
    annotate->setChecked(m_annotateMode);
    annotate->setEnabled(hasView);
    textLabel = menu.addAction(tr("Add Text Label (T)"));
    textLabel->setEnabled(hasView);
    exportReview = menu.addAction(tr("Export Review Image…"));
    exportReview->setEnabled(hasView);
    colorChecker = menu.addAction(tr("Color Checker dE00…"));
    colorChecker->setEnabled(hasView);
    blindTest = menu.addAction(tr("A/B Blind Test…"));
    blindTest->setEnabled(hasView);
    spectrum = menu.addAction(tr("FFT Spectrum"));
    spectrum->setCheckable(true);
    spectrum->setChecked(m_spectrumVisible);
    spectrum->setEnabled(hasView);
    cvBoxes = menu.addAction(tr("Load CV Boxes…"));
    cvBoxes->setEnabled(hasView);
}

bool ImageViewer::handleReviewContextAction(QAction *chosen, QAction *annotate, QAction *textLabel,
                                            QAction *exportReview, QAction *colorChecker,
                                            QAction *blindTest, QAction *spectrum, QAction *cvBoxes)
{
    if (chosen == annotate)
    {
        setAnnotateMode(!m_annotateMode);
        return true;
    }
    if (chosen == textLabel)
    {
        addTextAnnotationAtCursor();
        return true;
    }
    if (chosen == exportReview)
    {
        exportAnnotatedImage();
        return true;
    }
    if (chosen == colorChecker)
    {
        measureColorChecker();
        return true;
    }
    if (chosen == blindTest)
    {
        BlindTestDialog dialog(this);
        dialog.setSources(m_currentPath, m_currentPath);
        dialog.exec();
        return true;
    }
    if (chosen == spectrum)
    {
        setSpectrumVisible(!m_spectrumVisible);
        return true;
    }
    if (chosen == cvBoxes)
    {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load CV Boxes"), QString(),
                                                          tr("JSON (*.json);;All files (*)"));
        if (!path.isEmpty())
            loadCvBoxes(path);
        return true;
    }
    return false;
}

void ImageViewer::drawAnnotations(QPainter &painter) const
{
    if (!m_annotateMode || m_view.scale <= 0.0)
        return;
    // Markup is stored in image coordinates, so it is transformed by the same
    // view matrix as the photo.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto toScreen = [this](const QPointF &p)
    {
        return QPointF(p.x() * m_view.scale + m_view.offsetX,
                       p.y() * m_view.scale + m_view.offsetY);
    };
    const QVector<mviewer::ui::Annotation> items = m_annotations.annotations();
    for (const mviewer::ui::Annotation &a : items)
    {
        painter.setPen(QPen(a.color, 2));
        painter.setBrush(Qt::NoBrush);
        if (a.kind == mviewer::ui::AnnotationKind::Arrow)
        {
            painter.drawLine(toScreen(a.line.p1()), toScreen(a.line.p2()));
        }
        else
        {
            painter.drawRect(QRectF(toScreen(a.rect.topLeft()), toScreen(a.rect.bottomRight())));
            if (a.kind == mviewer::ui::AnnotationKind::Text && !a.text.isEmpty())
                painter.drawText(toScreen(a.rect.topLeft()), a.text);
        }
    }
    if (m_annotating)
    {
        painter.setPen(QPen(QColor(255, 200, 40), 2));
        painter.drawLine(m_annStart, m_annEnd);
    }
    painter.restore();
}

void ImageViewer::ensureAnnotationsForCurrentPath()
{
    if (m_annotations.sourcePath() == m_currentPath)
        return;
    m_annotations.setSourcePath(m_currentPath);
    m_annotations.load();
}

void ImageViewer::handleAnnotateMouseEvent(QMouseEvent *event, int phase)
{
    ensureAnnotationsForCurrentPath();
    if (!m_annotateMode)
        return;
    const QPointF imagePoint((event->pos().x() - m_view.offsetX) / m_view.scale,
                             (event->pos().y() - m_view.offsetY) / m_view.scale);

    if (phase == 0)
    {
        if (!hasDisplayImage() || m_view.scale <= 0.0)
            return;
        // Right button removes the topmost annotation under the cursor.
        if (event->button() == Qt::RightButton)
        {
            const int hit = m_annotations.indexAt(imagePoint);
            if (hit >= 0)
            {
                m_annotations.removeAt(hit);
                m_annotations.save();
                update();
            }
            return;
        }
        if (event->button() != Qt::LeftButton)
            return;
        m_annotating = true;
        m_annStart = m_annEnd = event->pos();
        m_annotateArrow = event->modifiers().testFlag(Qt::ShiftModifier);
        update();
        return;
    }

    if (phase == 1)
    {
        if (!m_annotating)
            return;
        m_annEnd = event->pos();
        update();
        return;
    }

    // phase == 2: release commits the primitive.
    if (!m_annotating)
        return;
    m_annotating = false;
    if (event->button() != Qt::LeftButton)
        return;
    const QLineF line(QPointF((m_annStart.x() - m_view.offsetX) / m_view.scale,
                              (m_annStart.y() - m_view.offsetY) / m_view.scale),
                      imagePoint);
    if (line.length() < 2.0)
        return;

    mviewer::ui::Annotation a;
    if (m_annotateArrow)
    {
        a.kind = mviewer::ui::AnnotationKind::Arrow;
        a.line = line;
    }
    else
    {
        a.kind = mviewer::ui::AnnotationKind::Rect;
        a.rect = QRectF(line.p1(), line.p2()).normalized();
    }
    m_annotations.add(a);
    m_annotations.save();
    update();
}

void ImageViewer::exportAnnotatedImage()
{
    ensureAnnotationsForCurrentPath();
    if (m_currentPath.isEmpty())
        return;
    QImage canvas;
    if (m_frame && m_frame->isValid())
        canvas = mvcore::toQImageFast(m_frame->pixels());
    else
        canvas = displayRaster();
    if (canvas.isNull())
    {
        emit statusMessageRequested(tr("Nothing to export: no image is loaded"), 4000);
        return;
    }
    canvas = canvas.convertToFormat(QImage::Format_ARGB32);

    const QVector<mviewer::ui::Annotation> items = m_annotations.annotations();
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const mviewer::ui::Annotation &a : items)
    {
        p.setPen(QPen(a.color, 3));
        p.setBrush(Qt::NoBrush);
        if (a.kind == mviewer::ui::AnnotationKind::Arrow)
        {
            p.drawLine(a.line);
            const double angle = std::atan2(a.line.dy(), a.line.dx());
            const double head = 14.0;
            for (const double sign : {-1.0, 1.0})
            {
                const double theta = angle + sign * 2.6;
                p.drawLine(a.line.p2(),
                           a.line.p2() - QPointF(std::cos(theta) * head, std::sin(theta) * head));
            }
        }
        else if (a.kind == mviewer::ui::AnnotationKind::Rect)
        {
            p.drawRect(a.rect);
        }
        else
        {
            p.drawRect(a.rect);
            if (!a.text.isEmpty())
                p.drawText(a.rect.adjusted(4, 4, 0, 0), Qt::TextSingleLine, a.text);
        }
    }
    p.end();

    const QFileInfo info(m_currentPath);
    const QString destination = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                                QStringLiteral("-review.png");
    if (!canvas.save(destination, "PNG"))
    {
        emit statusMessageRequested(
            tr("Could not write the review image") + QStringLiteral(": ") + destination, 5000);
        return;
    }
    if (!m_annotations.save())
    {
        emit statusMessageRequested(tr("Review image saved, but the annotation sidecar was not"),
                                    5000);
        return;
    }
    emit statusMessageRequested(
        tr("Review image: %1 (%2 annotations)").arg(destination).arg(items.size()), 5000);
}

void ImageViewer::addTextAnnotationAtCursor()
{
    ensureAnnotationsForCurrentPath();
    if (!m_annotateMode || !hasDisplayImage() || m_view.scale <= 0.0)
        return;
    int ix = m_lastHoverX;
    int iy = m_lastHoverY;
    if (ix < 0 || iy < 0)
    {
        ix = displaySize().width() / 2;
        iy = displaySize().height() / 2;
    }
    bool ok = false;
    const QString text =
        QInputDialog::getText(this, tr("Annotation"), tr("Label text"), QLineEdit::Normal, {}, &ok);
    if (!ok || text.trimmed().isEmpty())
        return;
    mviewer::ui::Annotation a;
    a.kind = mviewer::ui::AnnotationKind::Text;
    a.text = text.trimmed();
    a.rect = QRectF(ix, iy, 1, 1);
    m_annotations.add(a);
    m_annotations.save();
    update();
}
