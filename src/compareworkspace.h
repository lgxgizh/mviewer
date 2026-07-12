#pragma once

#include "core/CompareEngine.h"

#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QGridLayout>
#include <QPixmap>
#include <QPointF>
#include <QHash>

// CompareWorkspace：多图同步比较工作区
class CompareWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit CompareWorkspace(QWidget *parent = nullptr);

    void setImages(const QStringList &paths);

    bool isSyncEnabled() const;
    void setSyncEnabled(bool on);

    CompareEngine &engine() { return m_engine; }

signals:
    void syncToggled(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void rebuildCells();
    void fitAll();

    CompareEngine m_engine;
    QCheckBox *m_syncChk = nullptr;
    QWidget *m_grid = nullptr;
    QGridLayout *m_layout = nullptr;
    QList<QLabel*> m_cells;
    QHash<int, double> m_imageScale;
    QHash<int, QPointF> m_imageOffset;
};
