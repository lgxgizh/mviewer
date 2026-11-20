#pragma once

#include <QImage>
#include <QLineF>
#include <QWidget>

namespace mviewer::ui
{

// Video-scope and line-profile readout for one frame.
//
// Modes:
//   LineProfile  - RGB and luma sampled along a draggable line (edge overshoot,
//                  ringing, skin-tone steps).
//   LumaWaveform - per-column luma range (the classic "waveform").
//   RgbParade    - the same range per R / G / B channel, side by side.
//   Vectorscope  - chroma distribution on the U/V plane.
//
// Everything is derived from the QImage handed to setImage(); the widget keeps
// no state and does no I/O, so repaints stay cheap enough for live zoom/pan.
class ScopeWidget : public QWidget
{
    Q_OBJECT

  public:
    enum class Mode
    {
        LineProfile,
        LumaWaveform,
        RgbParade,
        Vectorscope
    };

    explicit ScopeWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setMode(Mode mode);
    Mode mode() const
    {
        return m_mode;
    }

    /// Profile line in IMAGE pixel coordinates (not widget coordinates).
    void setProfileLine(const QLineF &line);
    void clearProfileLine();
    bool hasProfileLine() const
    {
        return m_hasLine;
    }

    static QString modeLabel(Mode mode);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    void paintLineProfile(QPainter &p, const QRectF &target) const;
    void paintWaveform(QPainter &p, const QRectF &target, bool parade) const;
    void paintVectorscope(QPainter &p, const QRectF &target) const;

    /// Sample at most `maxSamples` pixels along the line, nearest neighbour.
    QVector<QPoint> sampleLinePoints(int maxSamples) const;

    QImage m_image;
    Mode m_mode = Mode::LumaWaveform;
    QLineF m_line;
    bool m_hasLine = false;
};

} // namespace mviewer::ui
