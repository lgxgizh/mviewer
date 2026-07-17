#pragma once

// BlinkController: owns blink state for blink comparison mode.
class BlinkController
{
  public:
    explicit BlinkController(int imageCount = 0);

    void setBlinkIndex(int idx);
    void clearBlink();
    int blinkIndex() const
    {
        return m_blinkIndex;
    }
    bool isBlinking() const
    {
        return m_blinkIndex >= 0;
    }

    void setImageCount(int n)
    {
        m_imageCount = n;
    }

  private:
    int m_imageCount = 0;
    int m_blinkIndex = -1;
};
