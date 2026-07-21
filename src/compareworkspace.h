#pragma once

#include "core/compare/CompareEngine.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QStringList>
#include <QWidget>
#include <memory>

class QScrollArea;
class RawImageView;

// CompareWorkspace：多图同步比较工作区
class CompareWorkspace : public QWidget
{
    Q_OBJECT

  public:
    explicit CompareWorkspace(QWidget *parent = nullptr);
    ~CompareWorkspace();

    void setImages(const QStringList &paths);

    bool isSyncEnabled() const;
    void setSyncEnabled(bool on);

    CompareEngine &engine()
    {
        return m_engine;
    }

    // M12.1: last ROI applied to the compare cells (for Workspace persistence).
    // Empty selection (width<=0) means no ROI was set.
    mviewer::domain::Selection currentROI() const
    {
        return m_lastSelection;
    }

    // M12.1: re-apply a persisted ROI (delegates to the internal all-cells apply).
    void applyROI(const mviewer::domain::Selection &sel)
    {
        applySelectionToAll(sel);
    }

    // M12.2 (G2-ext): the image paths currently loaded into the compare cells.
    // Used by Workspace persistence to capture session context per image.
    QStringList comparedImages() const;

    // M15: full compare-session snapshot (sync mode, per-cell zoom/pan, shared
    // transform, ROI) for Workspace persistence.
    mviewer::domain::CompareSession compareSession() const
    {
        return m_engine.session();
    }

    // M15: restore a persisted CompareSession: sync mode, shared zoom/pan, and
    // per-cell transforms. Call after setImages() so the engine owns the frames
    // the transforms reference. The selection/ROI is applied via applyROI().
    void applySession(const mviewer::domain::CompareSession &s);

  signals:
    void syncToggled(bool on);
    // Hover pixel read from any cell, formatted for the status bar. Empty string clears.
    void pixelInfo(const QString &text);

  protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    void rebuildCells();
    void fitAll();
    void applySelectionToAll(const mviewer::domain::Selection &sel);

    CompareEngine m_engine;
    QCheckBox *m_syncZoomChk = nullptr;
    QCheckBox *m_syncDragChk = nullptr;
    bool m_syncZoom = true;
    bool m_syncDrag = true;
    QWidget *m_grid = nullptr;
    QGridLayout *m_layout = nullptr;
    QList<QLabel *> m_cellLabels;
    QList<RawImageView *> m_cellViews;
    bool m_dragging = false;
    QPoint m_lastMouse;
    int m_dragIdx = -1;
    mviewer::domain::Selection m_lastSelection; // M12.1: last applied ROI

    // M14-3: blink (flicker) compare
    QCheckBox *m_blinkChk = nullptr;
    QTimer *m_blinkTimer = nullptr;
    bool m_blinkState = false;
    void toggleBlink();
    void applyBlink(bool state);

    // Paints the most recent async diff result (from the EventBus) onto the
    // matching cell. Called on the UI thread via QueuedConnection.
    void refreshDiffOverlay();

    // EventBus subscription id for "CompareEngine.DiffResult"; unsubscribed in
    // the destructor because the EventBus is a process-global singleton and a
    // live subscription into a destroyed widget would crash.
    int m_diffSubId = 0;
};