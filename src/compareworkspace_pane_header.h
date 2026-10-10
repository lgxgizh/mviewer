#pragma once

#include <QString>

class QVBoxLayout;
class QWidget;

namespace mviewer::ui
{

// Index chip and filename share one header row so the badge cannot cover the name.
QWidget *installComparePaneHeader(QWidget *cell, QVBoxLayout *layout);
void updateComparePaneHeaderName(QWidget *cell, const QString &name, const QString &fullPath,
                                 bool showName);
void updateComparePaneHeaderBadge(QWidget *cell, int indexOneBased, bool show);

} // namespace mviewer::ui
