#include "imageviewer.h"

#include "display/DisplayColorContextProvider.h"

#include <QWindow>

namespace
{

QWindow *hostWindowHandle(const QWidget *widget)
{
    if (!widget)
        return nullptr;
    if (QWindow *own = widget->windowHandle())
        return own;
    const QWidget *host = widget->window();
    return host ? host->windowHandle() : nullptr;
}

} // namespace

void ImageViewer::onDisplayScreenChanged()
{
    QWindow *handle = hostWindowHandle(this);
    if (!handle)
        return;
    if (DisplayColorContextProvider::targetUnchanged(handle, m_displayColorTarget))
        return;
    // A screen change replaces the colour space, not the user's adjustments:
    // rebuilding the context without adjust silently reset exposure / gamma /
    // auto-stretch whenever the window crossed a monitor boundary.
    mviewer::core::DisplayColorContext next = DisplayColorContextProvider::forWindow(handle);
    next.adjust = m_displayColorTarget.adjust;
    setDisplayColorContext(next);
}

void ImageViewer::bindDisplayColorScreen()
{
    QWindow *handle = hostWindowHandle(this);
    if (!handle)
        return;
    connect(handle, &QWindow::screenChanged, this, &ImageViewer::onDisplayScreenChanged,
            Qt::UniqueConnection);
    if (DisplayColorSpaceHub *hub = DisplayColorContextProvider::colorSpaceHub())
    {
        connect(hub, &DisplayColorSpaceHub::colorSpaceChanged, this,
                &ImageViewer::onDisplayScreenChanged, Qt::UniqueConnection);
    }
    if (DisplayColorContextProvider::targetUnchanged(handle, m_displayColorTarget))
        return;
    setDisplayColorContext(DisplayColorContextProvider::forWindow(handle));
}
