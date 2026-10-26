// Temporary switch (Space hold / 1-8 / X swap): every overlay floating on a
// pane follows the image the pane actually shows, then returns on release.
// The pane histogram comes from a per-image cache, so a switch never submits
// a new Analysis batch.
bool waitForPaneRasters(CompareWorkspace *ws, int count, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        bool ready = true;
        for (int i = 0; ready && i < count; ++i)
        {
            RawImageView *view = comparePaneView(ws, i);
            ready = view && !view->displayImage().isNull();
        }
        if (ready)
            return true;
        pump(25);
    }
    return false;
}

QString paneHeaderPath(CompareWorkspace *ws, int index)
{
    QWidget *pane = ws->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(index));
    QLabel *name = pane ? pane->findChild<QLabel *>(QStringLiteral("paneHeaderName")) : nullptr;
    return name ? name->toolTip() : QString();
}

int64_t paneHistTotal(CompareWorkspace *ws, int index)
{
    auto *hw = ws->findChild<HistogramWidget *>(QStringLiteral("paneHistogram%1").arg(index));
    return hw && hw->histogramCount() > 0 ? hw->histogramTotal(0) : -1;
}

void sendSpace(QWidget *target, QEvent::Type type)
{
    QKeyEvent ev(type, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(target, &ev);
    pump(30);
}

void testTemporarySwitchOverlaysFollowImage(const QDir &dir)
{
    std::cout << "--- Compare: overlays follow the temporarily shown image ---\n";
    const QString a = writePng(dir, QStringLiteral("follow_a.png"), 40, 30, QColor(200, 30, 30));
    const QString b = writePng(dir, QStringLiteral("follow_b.png"), 64, 48, QColor(30, 30, 200));
    const int64_t totalA = 40 * 30;
    const int64_t totalB = 64 * 48;
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({a, b});
    dlg.resize(1100, 750);
    dlg.show();
    waitForCompareCount(ws, 2);
    CHECK(waitForPaneRasters(ws, 2), "Follow: both panes show their raster");

    auto *overlay = ws->findChild<QCheckBox *>(QStringLiteral("paneHistogramOverlayToggle"));
    CHECK(overlay != nullptr, "Follow: pane histogram overlay toggle exists");
    if (!overlay)
        return;
    overlay->setChecked(true);
    waitForHistogramTotal(ws, QStringLiteral("paneHistogram0"), totalA);
    waitForHistogramTotal(ws, QStringLiteral("paneHistogram1"), totalB);
    CHECK(paneHistTotal(ws, 0) == totalA && paneHistTotal(ws, 1) == totalB,
          "Follow: each pane histogram starts on its own image");

    RawImageView *view0 = comparePaneView(ws, 0);
    RawImageView *view1 = comparePaneView(ws, 1);
    if (!view0 || !view1)
        return;
    const QString name0 = view0->filenameOverlayText();
    const QString name1 = view1->filenameOverlayText();
    CHECK(name0.contains(QStringLiteral("follow_a")) && name1.contains(QStringLiteral("follow_b")),
          "Follow: filename overlays start on each pane's own image");
    const QString header0 = paneHeaderPath(ws, 0);
    const QString header1 = paneHeaderPath(ws, 1);

    auto &sched = TaskScheduler::instance();
    sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(10));
    pump(50);
    const uint64_t submitted = sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;

    for (int side = 0; side < 2; ++side)
    {
        RawImageView *target = side == 0 ? view0 : view1;
        const QString ownName = side == 0 ? name0 : name1;
        const QString otherName = side == 0 ? name1 : name0;
        const int64_t ownTotal = side == 0 ? totalA : totalB;
        const int64_t otherTotal = side == 0 ? totalB : totalA;
        const QString label = QStringLiteral("Follow pane %1: ").arg(side);

        ws->setFocus();
        pointAtComparePane(target);
        sendSpace(ws, QEvent::KeyPress);
        CHECK(target->hasTransientDisplay(), (label + "Space shows the other image").toStdString());
        CHECK(target->filenameOverlayText() == otherName,
              (label + "filename overlay names the shown image").toStdString());
        CHECK(paneHistTotal(ws, side) == otherTotal,
              (label + "histogram comes from the shown image").toStdString());
        CHECK(paneHeaderPath(ws, side) == (side == 0 ? header1 : header0),
              (label + "header path names the shown image").toStdString());
        CHECK(paneHistTotal(ws, 1 - side) == otherTotal,
              (label + "the other pane keeps its own histogram").toStdString());

        sendSpace(ws, QEvent::KeyRelease);
        CHECK(!target->hasTransientDisplay(), (label + "release restores the raster").toStdString());
        CHECK(target->filenameOverlayText() == ownName,
              (label + "release restores the filename overlay").toStdString());
        CHECK(paneHistTotal(ws, side) == ownTotal,
              (label + "release restores the histogram").toStdString());
        CHECK(paneHeaderPath(ws, side) == (side == 0 ? header0 : header1),
              (label + "release restores the header path").toStdString());
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted == submitted,
          "Follow: switching reuses cached histograms (no Analysis submission)");

    // X swap exchanges the images, so names and histograms must swap with them.
    tapCompareKey(ws, Qt::Key_X);
    pump(50);
    CHECK(view0->filenameOverlayText() == name1 && paneHistTotal(ws, 0) == totalB,
          "Follow: X swap moves name and histogram with the image");
    tapCompareKey(ws, Qt::Key_X);
    pump(50);
    CHECK(view0->filenameOverlayText() == name0 && paneHistTotal(ws, 0) == totalA,
          "Follow: second X swap restores name and histogram");
    dlg.close();
}

void testUsmTitlesFollowHold(const QDir &dir)
{
    std::cout << "--- USM compare: pane titles follow the Space hold ---\n";
    const QString imgPath =
        writePng(dir, QStringLiteral("usm_follow.png"), 64, 64, QColor(90, 140, 210));
    UsmCompareDialog dlg(imgPath);
    dlg.resize(900, 600);
    dlg.show();
    auto *leftTitle = dlg.findChild<QLabel *>(QStringLiteral("usmLeftTitle"));
    auto *rightTitle = dlg.findChild<QLabel *>(QStringLiteral("usmRightTitle"));
    CHECK(leftTitle && rightTitle, "USM follow: pane titles are discoverable");
    if (!leftTitle || !rightTitle)
        return;
    RawImageView *left = leftTitle->parentWidget()->findChild<RawImageView *>();
    RawImageView *right = rightTitle->parentWidget()->findChild<RawImageView *>();
    QElapsedTimer t;
    t.start();
    while ((!left || !right || left->displayImage().isNull() || right->displayImage().isNull()) &&
           t.elapsed() < 15000)
        pump(25);
    if (!left || !right || left->displayImage().isNull() || right->displayImage().isNull())
    {
        CHECK(false, "USM follow: both views show a raster");
        return;
    }
    const QString leftText = leftTitle->text();
    const QString rightText = rightTitle->text();
    CHECK(leftText != rightText, "USM follow: titles differ before the hold");

    pointAtComparePane(left);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&dlg, &press);
    pump(30);
    if (left->hasTransientDisplay())
    {
        CHECK(leftTitle->text() == rightText, "USM follow: held pane title names the shown image");
        CHECK(rightTitle->text() == rightText, "USM follow: other pane keeps its title");
    }
    else
    {
        CHECK(false, "USM follow: Space hold switches the hovered pane");
    }
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&dlg, &release);
    pump(30);
    CHECK(leftTitle->text() == leftText && rightTitle->text() == rightText,
          "USM follow: release restores both titles");
    dlg.close();
}
