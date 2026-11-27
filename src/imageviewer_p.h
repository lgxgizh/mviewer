#pragma once

// ImageViewer's passive state bundle (pimpl-style `d`).
//
// These members are presentation state that only ImageViewer's own translation
// units touch: mouse drag endpoints for the drawing tools, panel pointers and
// the annotation/cv-overlay stores. They live here so imageviewer.h stays well
// under its line budget and stops pulling heavy UI headers (annotationstore,
// cvoverlay) into every TU that includes it.
//
// Deliberately NOT an object-oriented controller: the paint path reads these
// fields directly, and introducing callback boundaries around them would cost a
// lot of churn for no product or testability gain. Only promote a sub-structure
// out into its own class when it needs unit tests without a QWidget, or reuse in
// another host (see compareworkspace's CvOverlay for that precedent).

#include "imageviewer.h"

#include "annotationstore.h"
#include "cvoverlay.h"
#include "widgets/displayadjustcontroller.h"

#include <QLineF>
#include <QPoint>

namespace mviewer::ui
{
class ScopeWidget;
}

struct ImageViewer::Private
{
    // Line-profile tool: drag a line, then read RGB/luma along it.
    struct ProfileState
    {
        bool mode = false;
        bool dragging = false;
        QPoint start;
        QPoint end;
        bool lineActive = false;
        QLineF lineImage;
    } profile;

    // Video scopes: a corner panel with four switchable views.
    struct ScopeState
    {
        bool visible = false;
        int mode = 0;
        mviewer::ui::ScopeWidget *widget = nullptr;
    } scope;

    // Review markup; the store reloads itself when the path changes.
    struct AnnotateState
    {
        bool mode = false;
        bool annotating = false;
        bool arrow = false;
        QPoint start;
        QPoint end;
        mviewer::ui::AnnotationStore store;
        bool lastRemovedValid = false;
        mviewer::ui::Annotation lastRemoved;
    } annotate;

    // Floating tools and their panels.
    struct ToolState
    {
        std::unique_ptr<mviewer::ui::DisplayAdjustController> adjust; // lazy; owns the panel
        bool spectrumVisible = false;
        QWidget *spectrumPanel = nullptr;
        mviewer::ui::CvOverlay cvOverlay;
    } tools;
};
