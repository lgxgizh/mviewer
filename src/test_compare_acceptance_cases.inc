void testMultiImageEntry(const QStringList &paths8)
{
    std::cout << "── Compare B#1/B#2/B#8: 4/8-image entry + controls ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1100, 750);
    dlg.show();
    pump(50);

    // 4-up from a 6-image selection.
    ws->setImagePool(paths8.mid(0, 6));
    ws->applyLayoutPreset(4);
    waitForCompareCount(ws, 4);
    CHECK(ws->comparedImageCount() == 4, "B#1: 4-image compare entry loads 4 images");

    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动"));
    CHECK(split && overlay, "B#8: split/overlay controls exist");
    if (split && overlay)
    {
        CHECK(!split->isEnabled() && !overlay->isEnabled(),
              "B#8: split/overlay disabled for 4 images");
        const bool hasExplain = !split->toolTip().isEmpty() && split->toolTip().contains("2");
        CHECK(hasExplain, "B#8: disabled split control explains its 2-image requirement");
    }

    // 8-up (4×2) from the full selection.
    ws->setImagePool(paths8);
    ws->applyLayoutPreset(8);
    waitForCompareCount(ws, 8);
    CHECK(ws->comparedImageCount() == 8, "B#1: 8-image compare entry loads 8 images");
    CHECK(ws->engine().layout().cols >= 2 && ws->engine().layout().rows >= 2,
          "B#2: 8-up produces a multi-row, multi-column grid");

    // Reference/focus must stay well-defined at any count.
    CHECK(sel.focused().isEmpty() || !ws->focusImagePath().isEmpty(),
          "B#2: focus image remains defined after layout switches");

    // 2-up back.
    ws->applyLayoutPreset(2);
    waitForCompareCount(ws, 2);
    CHECK(ws->comparedImageCount() == 2, "B#1: back to 2-image compare");
    if (split && overlay)
    {
        CHECK(split->isEnabled() && overlay->isEnabled(),
              "B#8: split/overlay re-enabled for exactly 2 images");
    }
}

// ─── B#4: mode switches preserve zoom + ROI ─────────────────────────────────
void testModePreservesState(const QString &a, const QString &b)
{
    std::cout << "── Compare B#4: mode switches preserve zoom/ROI ──\n";
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

    // Establish a non-trivial zoom + a ROI.
    const double zoom = 2.5;
    ws->engine().setScale(zoom);
    ws->applyROI({40, 30, 200, 120});
    pump(30);

    QCheckBox *blink = findChk(ws, QStringLiteral("闪烁对比"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    CHECK(blink && overlay && split, "B#4: blink/overlay/split controls present");
    if (!blink || !overlay || !split)
        return;

    // Cycle through every exclusive mode and back; zoom/ROI must survive.
    // (Blink is an independent axis — not part of the split/overlay family.)
    for (int i = 0; i < 3; ++i)
    {
        split->setChecked(true);
        pump(20);
        overlay->setChecked(true); // auto-unchecks split
        pump(20);
        overlay->setChecked(false);
        pump(20);
    }
    CHECK(qAbs(ws->engine().syncTransform().scale - zoom) < 1e-9,
          "B#4: zoom survives blink/overlay mode cycling");
    const auto roi = ws->currentROI();
    CHECK(roi.width == 200 && roi.height == 120 && roi.x == 40 && roi.y == 30,
          "B#4: ROI survives mode switches");
    CHECK(!split->isChecked() && !overlay->isChecked(),
          "B#4: no exclusive mode left stuck on after the cycle");

    // Diff-highlight toggling also preserves state.
    QCheckBox *diffHl = findChk(ws, QStringLiteral("差异高亮"));
    if (diffHl)
    {
        diffHl->setChecked(true);
        pump(30);
        diffHl->setChecked(false);
        pump(30);
        CHECK(qAbs(ws->engine().syncTransform().scale - zoom) < 1e-9,
              "B#4: zoom survives diff-highlight toggling");
    }
}

// M49: the column-driven grid and momentary A<-B presentation are UI-only
// contracts. The engine/session/SelectionModel must not change while held.
void testLayoutAndMomentaryCompare(const QDir &dir)
{
    std::cout << "── M49: exact grid + momentary Compare ──\n";
    const QString longA = writePng(
        dir, QStringLiteral("a very long 中文 filename for caption elision 0123456789.png"), 80, 60,
        QColor(240, 40, 40));
    const QString b = writePng(dir, QStringLiteral("B 蓝色.png"), 80, 60, QColor(40, 80, 240));

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({longA, b});
    dlg.resize(1200, 760);
    dlg.show();
    waitForCompareCount(ws, 2);
    pump(150);

    auto *grid = ws->findChild<QWidget *>(QStringLiteral("compareGridPage"));
    auto *pane0 = ws->findChild<QWidget *>(QStringLiteral("comparePane0"));
    auto *pane1 = ws->findChild<QWidget *>(QStringLiteral("comparePane1"));
    auto *caption = ws->findChild<QLabel *>(QStringLiteral("paneCaption0"));
    CHECK(grid && pane0 && pane1, "M49: two compare panes are present");
    if (grid && pane0 && pane1)
    {
        const QRect r0(pane0->mapTo(grid, QPoint(0, 0)), pane0->size());
        const QRect r1(pane1->mapTo(grid, QPoint(0, 0)), pane1->size());
        const int gap = r1.left() - r0.right() - 1;
        CHECK(std::abs(r0.width() - r1.width()) <= 1 && r0.left() == 0 && gap >= 0 &&
                  r1.right() == grid->width() - 1,
              "M49: default two-pane grid is an exact 50/50 split");
    }
    const QString fullName = QFileInfo(longA).fileName();
    CHECK(caption && !caption->isVisible() && caption->toolTip().contains(fullName) &&
              caption->text() == caption->toolTip() && !caption->text().contains(QChar(0x2026)),
          "M49: below-pane caption stays hidden and stores the full filename");

    auto *button = ws->findChild<QPushButton *>(QStringLiteral("temporaryCompareButton"));
    auto *view0 = pane0 ? pane0->findChild<RawImageView *>() : nullptr;
    auto *view1 = pane1 ? pane1->findChild<RawImageView *>() : nullptr;
    CHECK(button && button->isVisible() && button->isEnabled(),
          "M49: momentary Compare control is visible and enabled for two loaded panes");
    CHECK(view0 && view1 && !view0->image().isNull() && !view1->image().isNull(),
          "M49: both pane display rasters are materialized");
    CHECK(view0 && caption && view0->filenameOverlayVisible() &&
              view0->filenameOverlayText() == caption->toolTip() &&
              view0->filenameOverlayText().contains(fullName),
          "M49: top overlay shows the full filename without elision");
    if (!button || !view0 || !view1 || view0->image().isNull() || view1->image().isNull())
        return;

    const auto before = ws->engine().session();
    const QStringList selectionBefore = sel.selection();
    const qint64 aKey = view0->image().cacheKey();
    const qint64 bKey = view1->image().cacheKey();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(button->rect().center()), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(button, &press);
    pump(20);
    CHECK(view0->hasTransientDisplay() && view0->displayImage().cacheKey() == bKey &&
              view0->image().cacheKey() == aKey,
          "M49: pressing momentary Compare presents B without replacing A state");
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(button->rect().center()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(button, &release);
    pump(20);
    CHECK(!view0->hasTransientDisplay() && view0->displayImage().cacheKey() == aKey,
          "M49: releasing momentary Compare restores A");

    ws->setFocus();
    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(ws, &spacePress);
    pump(20);
    CHECK(view0->hasTransientDisplay() && view0->displayImage().cacheKey() == bKey,
          "M49: Space uses the same momentary presentation path");
    QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(ws, &spaceRelease);
    pump(20);

    const auto after = ws->engine().session();
    CHECK(!view0->hasTransientDisplay() && sel.selection() == selectionBefore &&
              before.imageIds == after.imageIds && before.cells.size() == after.cells.size() &&
              before.selection.x == after.selection.x && before.selection.y == after.selection.y &&
              before.selection.w == after.selection.w && before.selection.h == after.selection.h,
          "M49: momentary Compare leaves engine and SelectionModel state unchanged");
}

// ─── B#6: continuous pair navigation preserves mode ─────────────────────────
void testContinuousNav(const QStringList &paths6)
{
    std::cout << "── Compare B#6: continuous pair navigation ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImagePool(paths6);
    ws->setNavWindow(2);
    dlg.resize(1100, 750);
    dlg.show();
    pump(80);

    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    CHECK(overlay != nullptr, "B#6: overlay control present");
    if (!overlay)
        return;
    overlay->setChecked(true);
    pump(30);

    // Walk forward through the pool; the overlay mode must persist.
    int pairs = 0;
    while (ws->hasNextPair())
    {
        ws->nextPair();
        waitForCompareCount(ws, 2);
        ++pairs;
        if (!overlay->isChecked())
            break;
    }
    CHECK(pairs >= 2, "B#6: next-pair walks at least two pairs");
    CHECK(overlay->isChecked(), "B#6: overlay mode persists across pair navigation");

    // Walk backward; mode + focus stay coherent.
    int back = 0;
    while (ws->hasPrevPair() && back < 4)
    {
        ws->prevPair();
        waitForCompareCount(ws, 2);
        ++back;
    }
    CHECK(back >= 2, "B#6: prev-pair walks back");
    CHECK(overlay->isChecked(), "B#6: mode persists walking backward");
    CHECK(!ws->focusImagePath().isEmpty(), "B#6: focus image defined after navigation");
}

// B#6 regression: MainWindow seeds the pool before the asynchronous initial
// compare load.  The explicit current window must therefore establish both
// the pair index and the navigation step before setImages() finishes.
void testNonFirstWindowNav(const QStringList &paths6)
{
    std::cout << "── Compare B#6: selected non-first window navigation ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1100, 750);
    dlg.show();
    pump(80);

    const QStringList current{paths6[2], paths6[3]};
    ws->setImagePool(paths6, current);
    ws->setImages(current);
    waitForCompareLoadFinished(ws, 2);
    CHECK(ws->comparedImages() == current,
          "B#6: an explicit current window loads the selected C/D pair");
    CHECK(ws->hasPrevPair() && ws->hasNextPair(),
          "B#6: non-first C/D window enables both previous and next pair");

    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    CHECK(overlay != nullptr, "B#6: selected-window navigation exposes overlay mode");
    if (!overlay)
        return;
    overlay->setChecked(true);
    pump(30);

    ws->nextPair();
    waitForCompareLoadFinished(ws, 2);
    const QStringList end{paths6[4], paths6[5]};
    CHECK(ws->comparedImages() == end, "B#6: next from C/D loads E/F without reopening Compare");
    CHECK(overlay->isChecked(), "B#6: next preserves overlay");
    CHECK(sel.selection() == end, "B#6: next publishes the complete E/F SelectionModel window");
    CHECK(sel.currentImage() == end.first(), "B#6: next focuses E within the E/F window");

    ws->prevPair();
    waitForCompareLoadFinished(ws, 2);
    CHECK(ws->comparedImages() == current, "B#6: previous from E/F returns to C/D");
    CHECK(overlay->isChecked(), "B#6: returning to C/D preserves overlay");
    CHECK(sel.selection() == current,
          "B#6: returning to C/D publishes the complete C/D SelectionModel window");
    CHECK(sel.currentImage() == current.first(), "B#6: returning to C/D focuses C");

    ws->prevPair();
    waitForCompareLoadFinished(ws, 2);
    const QStringList start{paths6[0], paths6[1]};
    CHECK(ws->comparedImages() == start, "B#6: previous from C/D reaches the first A/B pair");
    CHECK(overlay->isChecked(), "B#6: first pair keeps the compare mode");
    CHECK(sel.selection() == start,
          "B#6: first pair publishes the complete A/B SelectionModel window");
    CHECK(sel.currentImage() == start.first(), "B#6: first pair focuses A");
}

// B#6 regression: a selected non-adjacent pair must map Compare focus to the
// engine's pane path, not to the adjacent entry in the navigation pool.
void testNonAdjacentFocus(const QStringList &pool, const QString &pathA, const QString &pathC)
{
    std::cout << "── Compare B#6: non-adjacent focus mapping ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImagePool(pool, {pathA, pathC});
    ws->setImages({pathA, pathC});
    dlg.resize(1100, 750);
    dlg.show();
    waitForCompareLoadFinished(ws, 2);

    const QStringList pair{pathA, pathC};
    CHECK(ws->comparedImages() == pair, "B#6: non-adjacent A/C pair loads into Compare");
    sel.setSelection(pair, pathA);

    RawImageView *view1 = nullptr;
    for (RawImageView *view : ws->findChildren<RawImageView *>())
    {
        if (view && view->cellIndex() == 1)
        {
            view1 = view;
            break;
        }
    }
    CHECK(view1 != nullptr, "B#6: non-adjacent pair exposes pane 1");
    if (view1)
    {
        const QPoint center = view1->rect().center();
        QMouseEvent hover(QEvent::MouseMove, QPointF(center), Qt::NoButton, Qt::NoButton,
                          Qt::NoModifier);
        QApplication::sendEvent(view1, &hover);
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(center), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view1, &doubleClick);
        pump(60);
    }
    CHECK(ws->focusImagePath() == pathC,
          "B#6: focusing pane 1 resolves the actual non-adjacent C path");
    CHECK(sel.currentImage() == pathC,
          "B#6: non-adjacent focus updates SelectionModel.currentImage to C");
    CHECK(sel.focused() == pathC,
          "B#6: non-adjacent focus publishes C as the SelectionModel reference");
    CHECK(sel.selection() == pair,
          "B#6: non-adjacent focus preserves the Browse A/C multi-selection");
}

// P1: pane histogram consistency across ROI, rebuilds, and Blink.
void testPaneHistogramConsistency(const QString &a, const QString &b)
{
    std::cout << "--- Compare P1: pane histogram consistency ---\n";
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

    auto *perPane = ws->findChild<QCheckBox *>("perPaneHistogramToggle");
    auto *overlay = ws->findChild<QCheckBox *>("paneHistogramOverlayToggle");
    auto *roi = ws->findChild<QCheckBox *>("roiHistogramToggle");
    auto *blink = findChk(ws, QStringLiteral("闪烁对比"));
    CHECK(perPane && overlay && roi && blink, "P1: histogram and blink controls are discoverable");
    if (!perPane || !overlay || !roi || !blink)
        return;

    // P1 async histogram contract: histogram delivery is an Analysis batch,
    // never a synchronous compute on the UI thread. Drain whatever the load
    // generation queued so the release-gated blocker below is the only work in
    // front of the histogram submissions.
    auto &sched = TaskScheduler::instance();
    sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(10));
    pump(50);

    auto paneHist = [ws](int index)
    { return ws->findChild<HistogramWidget *>(QString("paneHistogram%1").arg(index)); };
    HistogramWidget *hist0 = paneHist(0);
    HistogramWidget *hist1 = paneHist(1);
    CHECK(hist0 && hist1, "P1: both pane histogram widgets exist");
    if (!hist0 || !hist1)
        return;

    // perPane populates the main analysis histogram asynchronously. The pane
    // overlays must stay empty while their own toggle is off.
    perPane->setChecked(true);
    waitForHistogramPopulated(ws, QStringLiteral("analysisHistogram"));
    CHECK(hist0->histogramCount() == 0 && hist1->histogramCount() == 0,
          "P1: pane overlays stay empty while the overlay toggle is off");

    // Occupy the single Analysis worker with a release-gated blocker (the M29
    // gate pattern from test_workflow_ux.cpp) so the overlay toggle's one
    // histogram batch stays observable instead of racing the UI thread.
    sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
    struct RestoreAnalysisThreads
    {
        ~RestoreAnalysisThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(
                TaskScheduler::Priority::Analysis, std::max(1, QThread::idealThreadCount() / 2));
        }
    } restoreAnalysis;

    std::mutex gateMtx;
    std::condition_variable gateCv;
    bool gateReleased = false;
    auto blocker =
        sched.submit(TaskScheduler::Priority::Analysis,
                     [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
                     {
                         std::unique_lock<std::mutex> lk(gateMtx);
                         gateCv.wait(lk, [&gateReleased] { return gateReleased; });
                     });
    struct ReleaseGateGuard
    {
        std::mutex &mtx;
        std::condition_variable &cv;
        bool &released;
        ~ReleaseGateGuard()
        {
            std::lock_guard<std::mutex> lk(mtx);
            released = true;
            cv.notify_all();
        }
    } releaseGate{gateMtx, gateCv, gateReleased};
    {
        QElapsedTimer wt;
        wt.start();
        while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
               wt.elapsed() < 5000)
            pump(10);
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
          "P1: release-gated blocker occupies the Analysis worker");
    const uint64_t submittedBefore = sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;

    // The overlay toggle submits exactly ONE Analysis batch for all pane
    // histograms; while the worker is blocked the panes stay empty because the
    // delivery is asynchronous, never a synchronous UI-thread refresh.
    overlay->setChecked(true);
    const uint64_t submittedAfter = sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
    CHECK(submittedAfter == submittedBefore + 1,
          "P1: overlay toggle submits exactly one Analysis histogram batch");
    CHECK(hist0->histogramCount() == 0 && hist1->histogramCount() == 0,
          "P1: pane histograms stay empty while the Analysis worker is blocked");

    // Release the gate and drain: both pane overlays must be delivered.
    {
        std::lock_guard<std::mutex> lk(gateMtx);
        gateReleased = true;
    }
    gateCv.notify_all();
    CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(15)),
          "P1: Analysis pool drains after the histogram gate release");
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram0"));
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram1"));
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramCount() == 1 && hist0->histogramTotal(0) > 0 &&
              hist1->histogramCount() == 1 && hist1->histogramTotal(0) > 0,
          "P1: independent pane overlays are populated");

    blink->setChecked(true);
    pump(30);
    auto *pane0 = ws->findChild<QWidget *>("comparePane0");
    auto *pane1 = ws->findChild<QWidget *>("comparePane1");
    auto *frame0 = ws->findChild<QWidget *>("paneHistogramFrame0");
    auto *frame1 = ws->findChild<QWidget *>("paneHistogramFrame1");
    auto *caption0 = ws->findChild<QLabel *>("paneCaption0");
    auto *caption1 = ws->findChild<QLabel *>("paneCaption1");
    CHECK(pane0 && pane1 && pane0->isVisible() && !pane1->isVisible(),
          "P1: Blink shows only the active pane container");
    CHECK(frame0 && frame1 && frame0->isVisible() && !frame1->isVisible(),
          "P1: Blink hides the inactive pane histogram frame");
    CHECK(caption0 && caption1 && !caption0->isVisible() && !caption1->isVisible(),
          "P1: filename captions stay hidden; names use the top overlay");

    // Exercise a timer tick too: exactly one whole pane remains visible when the
    // active image flips, and histogram frames follow that pane. Filename
    // captions stay hidden either way.
    pump(170);
    CHECK(pane0->isVisible() != pane1->isVisible(),
          "P1: repeated Blink ticks keep exactly one pane container visible");
    CHECK(frame0->isVisible() == pane0->isVisible() && frame1->isVisible() == pane1->isVisible(),
          "P1: pane histogram frames follow the active Blink pane");
    CHECK(caption0 && caption1 && !caption0->isVisible() && !caption1->isVisible(),
          "P1: filename captions stay hidden across Blink ticks");

    // A rebuild while Blink has detached one pane must delete both old panes,
    // preserve Blink visibility, and populate the new overlay widgets.
    ws->setImages({a, b});
    waitForCompareLoadFinished(ws, 2);
    const auto panes0 = ws->findChildren<QWidget *>("comparePane0");
    const auto panes1 = ws->findChildren<QWidget *>("comparePane1");
    CHECK(panes0.size() == 1 && panes1.size() == 1,
          "P1: Blink-active rebuild leaves no stale pane containers");
    CHECK(!panes0.isEmpty() && !panes1.isEmpty() &&
              panes0.front()->isVisible() != panes1.front()->isVisible(),
          "P1: Blink-active rebuild retains one active pane");
    CHECK(ws->engine().blinkController().isBlinking(),
          "P1: Blink-active rebuild restores engine Blink state");
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram0"));
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram1"));
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramTotal(0) > 0 && hist1->histogramTotal(0) > 0,
          "P1: Blink-active rebuild repopulates pane histograms");

    blink->setChecked(false);
    pump(50);
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram0"));
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram1"));
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramCount() == 1 && hist0->histogramTotal(0) > 0 &&
              hist1->histogramCount() == 1 && hist1->histogramTotal(0) > 0,
          "P1: Blink stop rebuild retains populated pane histograms");

    roi->setChecked(true);
    ws->applyROI({0, 0, 32, 32});
    waitForHistogramTotal(ws, QStringLiteral("analysisHistogram"), 1024);
    waitForHistogramTotal(ws, QStringLiteral("paneHistogram0"), 1024);
    waitForHistogramTotal(ws, QStringLiteral("paneHistogram1"), 1024);
    auto *mainHist = ws->findChild<HistogramWidget *>("analysisHistogram");
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(mainHist && mainHist->histogramCount() > 0 && mainHist->histogramTotal(0) == 1024,
          "P1: main histogram uses the 32x32 ROI total");
    CHECK(hist0 && hist1 && hist0->histogramTotal(0) == 1024 && hist1->histogramTotal(0) == 1024,
          "P1: pane histograms use the same 32x32 ROI total");
}

// B#7: mismatched resolutions and corrupt image handling.
void testDegradedImages(const QDir &dir)
{
    std::cout << "── Compare B#7: mismatched + corrupt images ──\n";
    const QString small = writePng(dir, "cmp_small.png", 64, 48, QColor(10, 10, 10));
    const QString large = writePng(dir, "cmp_large.png", 1024, 768, QColor(20, 20, 20));
    const QString portrait = writePng(dir, "cmp_portrait.png", 48, 64, QColor(30, 30, 30));
    const QString corrupt = dir.filePath("cmp_bad.png");
    QFile f(corrupt);
    const bool opened = f.open(QIODevice::WriteOnly);
    Q_UNUSED(opened); // C4834: QIODevice::open is [[nodiscard]] on Qt 6.10 headers
    const qint64 written = f.write("\x89PNG\r\n\x1a\n definitely not a png", 32);
    Q_UNUSED(written); // C4834: QIODevice::write is [[nodiscard]] on Qt 6.10 headers
    f.close();

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    QString warning;
    QObject::connect(ws, &CompareWorkspace::loadWarning, &dlg,
                     [&warning](const QString &text) { warning = text; });
    ws->setImages({small, large, portrait, corrupt});
    dlg.resize(1100, 750);
    dlg.show();
    waitForCompareCount(ws, 3);

    CHECK(ws->comparedImageCount() == 3,
          "B#7: corrupt image is excluded from compare without crashing");
    CHECK(warning.contains("无法加载") || warning.contains("无法加载"),
          "B#7: the load failure is surfaced to the user (loadWarning)");
    ws->engine().setScale(2.0);
    pump(30);
    CHECK(ws->engine().syncTransform().scale > 0, "B#7: zooming mixed sizes is functional");

    // A diff between mismatched sizes must report a sane result, not crash.
    ws->applyROI({0, 0, 32, 32});
    pump(60);

    // Next/prev navigation across the mixed pool stays safe.
    ws->setImagePool({small, large, portrait, corrupt});
    ws->setNavWindow(2);
    ws->nextPair();
    waitForCompareAtLeast(ws, 1);
    ws->nextPair();
    waitForCompareAtLeast(ws, 1);
    CHECK(ws->comparedImageCount() >= 1, "B#7: navigation over degraded pool keeps a grid");
    (void)corrupt;
}

// --- M28 P1-01: Compare open must NOT decode on the UI thread ---------------
// Regression: CompareWorkspace::setImages() used to call
// CompareEngine::setImages() -> ImageRepository::load() synchronously, so
// opening Compare froze the UI for every decoded frame. The async contract:
//   * setImages() returns immediately (submits decode work to the pool);
//   * no frames are applied synchronously (count == 0 right after the call);
//   * frames arrive asynchronously and the engine ends up fully populated.
//
// Contract: the panes themselves materialize asynchronously too.
// Progressive LOD: blank panes may submit a Decode-priority cheap first paint
// so cold open is not gated on Analysis; viewport-quality upgrade and the
// diff-analysis batch stay on Analysis. While Analysis is blocked, Decode may
// already deliver a provisional raster — panes are therefore not required to
// stay blank. Each load still submits at least the diff Analysis batch.
void testCompareLoadIsAsync(const QDir &dir)
{
    std::cout << "-- Compare M28 P1-01: async load (no UI-thread decode) --\n";
    // A 6000x4000 PNG takes far longer than 250ms to decode on CI hardware;
    // the synchronous implementation froze the UI for that long (or worse).
    const QString big = writePng(dir, "cmp_async_big.png", 6000, 4000, QColor(200, 120, 40));
    const QString small = writePng(dir, "cmp_async_small.png", 8, 8, QColor(40, 120, 200));

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1100, 750);
    dlg.show();

    // ── P1-01: pane materialization is asynchronous. Occupy the single
    // Analysis worker with a release-gated blocker (the M29 gate pattern from
    // test_workflow_ux.cpp) so every Analysis submission of this load queues
    // and stays observable instead of racing the UI thread. Drain first so the
    // blocker deterministically is the only task in front of this load.
    auto &sched = TaskScheduler::instance();
    sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(10));
    sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
    struct RestoreAnalysisThreads
    {
        ~RestoreAnalysisThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(
                TaskScheduler::Priority::Analysis, std::max(1, QThread::idealThreadCount() / 2));
        }
    } restoreAnalysis;

    std::mutex gateMtx;
    std::condition_variable gateCv;
    bool gateReleased = false;
    auto blocker =
        sched.submit(TaskScheduler::Priority::Analysis,
                     [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
                     {
                         std::unique_lock<std::mutex> lk(gateMtx);
                         gateCv.wait(lk, [&gateReleased] { return gateReleased; });
                     });
    struct ReleaseGateGuard
    {
        std::mutex &mtx;
        std::condition_variable &cv;
        bool &released;
        ~ReleaseGateGuard()
        {
            std::lock_guard<std::mutex> lk(mtx);
            released = true;
            cv.notify_all();
        }
    } releaseGate{gateMtx, gateCv, gateReleased};
    {
        QElapsedTimer wt;
        wt.start();
        while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
               wt.elapsed() < 5000)
            pump(10);
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
          "P1-01: release-gated blocker occupies the Analysis worker");
    const uint64_t submittedBefore = sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;

    QElapsedTimer t;
    t.start();
    ws->setImages({big, small});
    const qint64 setupMs = t.elapsed();
    CHECK(setupMs < 250, "setImages returns without decoding on the UI thread");
    CHECK(ws->comparedImageCount() == 0,
          "frames are delivered asynchronously, never synchronously applied");

    waitForCompareCount(ws, 2);
    CHECK(ws->comparedImageCount() == 2, "async compare load delivers all frames");

    // Decode has reached the UI (finishLoad -> rebuildCells ran on the UI
    // thread). Progressive LOD may already be painting a Decode-priority cheap
    // first frame while Analysis stays gate-blocked — that is intentional and
    // must not be a sync ImageData -> QImage conversion inside rebuildCells()
    // (covered by the setImages timing / count checks above).
    RawImageView *view0 = nullptr;
    RawImageView *view1 = nullptr;
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (!v)
            continue;
        if (v->cellIndex() == 0)
            view0 = v;
        else if (v->cellIndex() == 1)
            view1 = v;
    }
    CHECK(view0 && view1, "P1-01: two decoded panes exist after the async load");

    // Diff (and any non-provisional / upgrade display) still enqueue on Analysis.
    // Cheap provisional first paint races Decode and is not counted here.
    const uint64_t submittedAfter = sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
    CHECK(submittedAfter >= submittedBefore + 1,
          "P1-01: load generation submits at least the diff Analysis batch");

    // Give Decode a short window to deliver cheap provisional paint while
    // Analysis remains blocked. Small LOD targets may skip provisional and
    // stay blank until the Analysis display batch runs — both are valid.
    if (view0 && view1)
    {
        QElapsedTimer cheapWait;
        cheapWait.start();
        while ((view0->image().isNull() || view1->image().isNull()) && cheapWait.elapsed() < 2000)
            pump(25);
    }

    // Release the gate and drain both pools: both panes must materialize.
    {
        std::lock_guard<std::mutex> lk(gateMtx);
        gateReleased = true;
    }
    gateCv.notify_all();
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "P1-01: Decode pool drains after the gate release");
    CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(15)),
          "P1-01: Analysis pool drains after the gate release");
    {
        QElapsedTimer wt;
        wt.start();
        while ((!view0 || view0->image().isNull() || !view1 || view1->image().isNull()) &&
               wt.elapsed() < 5000)
            pump(25);
    }
    CHECK(view0 && view1 && !view0->image().isNull() && !view1->image().isNull(),
          "P1-01: panes materialize once Decode and Analysis drain");
    dlg.close();
}

// M42 P0/P1: real Compare display materialization must be viewport/LOD bounded.
// The source frames remain full resolution for diff/metrics, but a Fit-state
// pane must not retain a second QImage at the source dimensions.
void testLargeCompareDisplayBounded(const QDir &dir)
{
    std::cout << "-- Compare M42: 8-image display memory bounded --\n";
    QStringList largePaths;
    for (int i = 0; i < 8; ++i)
        largePaths << writePng(dir, QString("m42_large_%1.png").arg(i), 2400, 1600,
                               QColor(20 + i * 20, 40 + i * 10, 180 - i * 10));

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1200, 800);
    dlg.show();
    const auto rssBefore = mviewer::perf::MemoryTracker::instance().sample();
    ws->setImages(largePaths);
    waitForCompareCount(ws, 8, 30000);

    QElapsedTimer paneTimer;
    paneTimer.start();
    QList<RawImageView *> panes;
    while (paneTimer.elapsed() < 30000)
    {
        panes = ws->findChildren<RawImageView *>();
        if (panes.size() >= 8)
        {
            bool ready = true;
            for (RawImageView *pane : panes)
                ready = ready && pane && !pane->image().isNull() && pane->image().width() < 2400 &&
                        pane->image().height() < 1600;
            if (ready)
                break;
        }
        pump(25);
    }
    CHECK(ws->comparedImageCount() == 8 && panes.size() >= 8,
          "M42: 8-image Compare reaches the real pane path");

    size_t sourceBytes = 0;
    for (int i = 0; i < ws->engine().imageCount(); ++i)
    {
        const ImageFrame *frame = ws->engine().imageAt(i);
        if (frame)
            sourceBytes += frame->pixels().byteSize();
    }
    size_t displayBytes = 0;
    bool everyPaneIsLod = true;
    for (RawImageView *pane : panes)
    {
        if (!pane)
            continue;
        displayBytes += static_cast<size_t>(pane->image().sizeInBytes());
        everyPaneIsLod =
            everyPaneIsLod && pane->image().width() < 2400 && pane->image().height() < 1600;
    }
    CHECK(everyPaneIsLod, "M42: Fit panes use a display LOD below source dimensions");
    CHECK(sourceBytes > 0 && displayBytes * 2 < sourceBytes,
          "M42: 8-pane display bytes are materially below a second full-resolution copy");
    const auto rss = mviewer::perf::MemoryTracker::instance().sample();
    std::cout << "   source bytes=" << sourceBytes << " display bytes=" << displayBytes
              << " working-set KB before=" << rssBefore.processWorkingSetKB
              << " after=" << rss.processWorkingSetKB << "\n";

    // Exercise the real continuation path: zoom/pan, explicit diff, then close.
    if (!panes.isEmpty() && panes.first())
        panes.first()->zoom(2.0,
                            QPointF(panes.first()->width() / 2.0, panes.first()->height() / 2.0));
    if (QCheckBox *diff = findChk(ws, QStringLiteral("显示差异")))
        diff->setChecked(true);
    pump(100);
    dlg.close();
    auto &scheduler = TaskScheduler::instance();
    CHECK(scheduler.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(20)),
          "M42: Compare zoom/diff/exit drains Analysis work");
    const auto analysisMetrics = scheduler.metrics(TaskScheduler::PoolType::AnalysisPool);
    const auto graphMetrics = scheduler.graphMetrics();
    CHECK(analysisMetrics.pending == 0 && analysisMetrics.active_tasks == 0 &&
              analysisMetrics.queue_depth == 0,
          "M42: Compare Analysis pending/active/queue counters converge to zero");
    CHECK(graphMetrics.handles == 0 && graphMetrics.deferred == 0 &&
              graphMetrics.dep_graph_entries == 0 && graphMetrics.dependents_entries == 0,
          "M42: Compare scheduler handles and dependency graph converge to zero");
}

// M43 P0: the Inspector must sample the full-resolution ImageFrame, not the
// viewport-sized display QImage. This is deliberately an adversarial fixture:
// the source is much larger than the pane and its pixel values vary at every
// coordinate. The mismatch coordinate is discovered from the actual delivered
// LOD so the regression remains valid for different bounded target sizes.
void testInspectorUsesFullResolutionSource(const QDir &dir)
{
    std::cout << "-- Compare M43: Inspector source truth vs display LOD --\n";
    constexpr int sourceWidth = 2400;
    constexpr int sourceHeight = 1600;
    const QString path =
        writeHighFrequencyPng(dir, "m43_high_frequency.png", sourceWidth, sourceHeight);

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1000, 700);
    dlg.show();
    ws->setImages({path});
    waitForCompareCount(ws, 1, 30000);

    RawImageView *view = nullptr;
    QElapsedTimer ready;
    ready.start();
    while (ready.elapsed() < 30000)
    {
        const auto panes = ws->findChildren<RawImageView *>();
        if (!panes.isEmpty() && panes.front() && !panes.front()->image().isNull())
        {
            view = panes.front();
            break;
        }
        pump(25);
    }
    CHECK(view != nullptr, "M43: high-frequency Compare pane materializes");
    if (!view)
        return;

    QElapsedTimer lodReady;
    lodReady.start();
    while (lodReady.elapsed() < 30000 &&
           (view->image().width() >= sourceWidth || view->image().height() >= sourceHeight))
        pump(25);

    CHECK(view->image().width() < sourceWidth && view->image().height() < sourceHeight,
          "M43: high-frequency fixture is represented by a bounded display LOD");

    QPoint sourcePoint(-1, -1);
    for (int y = 17; y < sourceHeight - 17 && sourcePoint.x() < 0; ++y)
    {
        for (int x = 23; x < sourceWidth - 23; ++x)
        {
            const QPoint displayPoint = view->displayPointForSource(x, y);
            const QRgb source = highFrequencyPixel(x, y).rgb();
            if (view->image().pixel(displayPoint) != source)
            {
                sourcePoint = QPoint(x, y);
                break;
            }
        }
    }
    CHECK(sourcePoint.x() >= 0,
          "M43: adversarial fixture finds a source/LOD RGB mismatch coordinate");
    if (sourcePoint.x() < 0)
        return;

    auto *sideToggle = ws->findChild<QCheckBox *>("analysisPanelToggle");
    auto *table = ws->findChild<QTableWidget *>("pixelInspectorTable");
    CHECK(sideToggle && table, "M43: Pixel Inspector controls are discoverable");
    if (!sideToggle || !table)
        return;
    sideToggle->setChecked(true);
    pump(50);

    // Drive the same RawImageView pixelInfo boundary with a deterministic
    // source coordinate. The widget's real mouse path supplies these x/y
    // values; the direct signal invocation avoids rounding a sub-pixel source
    // coordinate when the Fit scale is below 1.
    auto inspectR = [&]()
    {
        QMetaObject::invokeMethod(view, "pixelInfo", Qt::DirectConnection,
                                  Q_ARG(int, sourcePoint.x()), Q_ARG(int, sourcePoint.y()),
                                  Q_ARG(int, 0), Q_ARG(int, 0), Q_ARG(int, 0), Q_ARG(bool, true));
        pump(80);
        return table->item(0, 2) ? table->item(0, 2)->text() : QString();
    };
    const QString exactR = inspectR();

    const QColor expected = highFrequencyPixel(sourcePoint.x(), sourcePoint.y());
    CHECK(exactR == QString::number(expected.red()),
          "M43: Inspector RGB is sampled from the exact full-resolution source pixel");
    CHECK(exactR != QString::number(qRed(view->image().pixel(
                        view->displayPointForSource(sourcePoint.x(), sourcePoint.y())))),
          "M43: Inspector RGB is not the display LOD sample");

    auto *kernel = ws->findChild<QComboBox *>("pixelInspectorKernelCombo");
    auto *stats = ws->findChild<QLabel *>("pixelInspectorStatsLabel");
    CHECK(kernel && stats, "M43: source-neighborhood controls are discoverable");
    if (kernel && stats)
    {
        kernel->setCurrentIndex(3); // 7×7
        pump(80);
        const QString expectedMean = QString::number(
            highFrequencyNeighborhoodMean(sourcePoint.x(), sourcePoint.y(), 7), 'f', 1);
        CHECK(stats->text().contains(QStringLiteral("μ=") + expectedMean),
              "M43: 7x7 neighborhood stats use full-resolution source pixels");
    }

    // The display representation may be replaced as zoom changes. The same
    // source coordinate must remain the same analysis value at 100%, 200%, and
    // after returning to Fit.
    view->zoom(2.0);
    pump(250);
    CHECK(inspectR() == QString::number(expected.red()),
          "M43: Inspector is stable after a zoom-driven LOD replacement");
    view->zoom(2.0);
    pump(250);
    CHECK(inspectR() == QString::number(expected.red()),
          "M43: Inspector is stable at a second zoom level");
    view->resetFit();
    pump(250);
    CHECK(inspectR() == QString::number(expected.red()),
          "M43: Inspector returns unchanged after Fit restoration");
}

// ─── M30: Pixel Inspector hover coalescing ──────────────────────
// The Compare inspector table is fed by up to two hover signals per mouse move
// when the synced crosshair is enabled (RawImageView::pixelInfo plus
// crosshairMoved), and every hover used to re-render the whole table by
// destroying and reallocating its QTableWidgetItems. The coalescer contract:
//   (a) a burst of hovers inside one event-loop turn renders exactly once,
//       with the latest coordinate;
//   (b) the pixelInfo + crosshairMoved pair of one physical hover renders
//       once, never twice;
//   (c) ordinary re-renders reuse the existing QTableWidgetItem objects;
//   (d) color-space / kernel / focus semantic changes refresh the current
//       coordinate correctly;
//   (e) a queued render is receiver-bound to the workspace, so destroying the
//       workspace with work pending is safe.
// The actual render count is surfaced as the dynamic QObject property
// "inspectorRenderCount" (narrowly scoped internal diagnostic, documented in
// compareworkspace.h) so the tests observe coalescing without widening the
// public API.
// M40: a superseded Compare batch must cancel queued repository requests and
// still converge its completion bookkeeping. This uses a release gate instead
// of wall-clock ordering so A -> B and scheduler rejection are deterministic.
void testCompareLoadCancellation(const QDir &dir)
{
    std::cout << "-- Compare M40: cancellable load batches --\n";
    const QString a = writePng(dir, "m40_cancel_a.png", 120, 80, QColor(220, 40, 40));
    const QString b = writePng(dir, "m40_cancel_b.png", 120, 80, QColor(40, 220, 40));
    const QString c = writePng(dir, "m40_cancel_c.png", 120, 80, QColor(40, 40, 220));
    const QString d = writePng(dir, "m40_cancel_d.png", 120, 80, QColor(220, 220, 40));

    auto &scheduler = TaskScheduler::instance();
    scheduler.drain(TaskScheduler::DecodePool, std::chrono::seconds(10));
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    struct RestoreDecodeThreads
    {
        ~RestoreDecodeThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(
                TaskScheduler::Priority::Decode, std::max(1, QThread::idealThreadCount() / 2));
        }
    } restoreThreads;

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool released = false;
    auto blocker =
        scheduler.submit(TaskScheduler::Priority::Decode,
                         [&gateMutex, &gateCv, &released](const TaskScheduler::TaskContext &)
                         {
                             std::unique_lock<std::mutex> lk(gateMutex);
                             gateCv.wait(lk, [&released] { return released; });
                         });
    while (scheduler.activeTaskCount(TaskScheduler::DecodePool) < 1)
        pump(10);

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    host.show();
    ws->setImages({a, b});
    ws->setImages({c, d});

    {
        std::lock_guard<std::mutex> lk(gateMutex);
        released = true;
    }
    gateCv.notify_all();
    CHECK(scheduler.drain(TaskScheduler::DecodePool, std::chrono::seconds(15)),
          "M40: gated DecodePool drains after superseding a batch");
    QElapsedTimer wait;
    wait.start();
    while (ws->comparedImageCount() != 2 && wait.elapsed() < 5000)
        pump(25);
    CHECK(ws->comparedImages() == QStringList({c, d}),
          "M40: newest compare set wins after A -> B queued cancellation");

    scheduler.pause(TaskScheduler::DecodePool);
    ws->setImages({a, b});
    pump(80);
    CHECK(ws->comparedImageCount() == 0, "M40: scheduler rejection does not hang batch completion");
    scheduler.resume(TaskScheduler::DecodePool);
    (void)blocker;
}

void testInspectorCoalescing(const QString &a, const QString &b)
{
    std::cout << "-- Compare M30: inspector hover coalescing --\n";

    QWidget host;
    auto *hostLay = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    hostLay->addWidget(ws);
    SelectionModel sel;
    ws->setSelectionModel(&sel);
    ws->setImages({a, b});
    host.resize(1100, 750);
    host.show();
    waitForCompareCount(ws, 2);

    auto *analysisToggle = ws->findChild<QCheckBox *>("analysisPanelToggle");
    auto *inspector = ws->findChild<QTableWidget *>("pixelInspectorTable");
    CHECK(analysisToggle && inspector, "M30: analysis toggle and inspector are discoverable");
    if (!analysisToggle || !inspector)
        return;
    analysisToggle->setChecked(true);
    pump(50);

    RawImageView *view0 = nullptr;
    RawImageView *view1 = nullptr;
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (!v)
            continue;
        if (v->cellIndex() == 0)
            view0 = v;
        else if (v->cellIndex() == 1)
            view1 = v;
    }
    {
        QElapsedTimer t;
        t.start();
        while ((!view0 || view0->image().isNull() || !view1 || view1->image().isNull()) &&
               t.elapsed() < 8000)
            pump(25);
    }
    CHECK(view0 && !view0->image().isNull() && view1 && !view1->image().isNull(),
          "M30: both panes materialize before hover testing");
    if (!view0 || view0->image().isNull() || !view1 || view1->image().isNull())
        return;

    // Deterministic transform so widget->image mapping is exact: at scale 1.0
    // with zero offset, a widget point near the center maps to a known image
    // pixel. All burst points stay inside the decoded pane image.
    ws->engine().setScale(1.0);
    ws->engine().setOffset(0.0, 0.0);
    ws->update();
    pump(30);

    auto renderCount = [ws]() { return ws->property("inspectorRenderCount").toULongLong(); };
    auto hoverAt = [view0](int dx, int dy)
    {
        const QPoint pt = view0->rect().center() + QPoint(dx, dy);
        QMouseEvent event(QEvent::MouseMove, QPointF(pt), Qt::NoButton, Qt::NoButton,
                          Qt::NoModifier);
        QApplication::sendEvent(view0, &event);
    };

    // (a) A burst of hovers inside one event-loop turn: exactly one render,
    // using the latest coordinate.
    const quint64 burstBase = renderCount();
    const int burstDx[] = {0, 3, 6, 9, 2, 7};
    const int burstDy[] = {0, 1, 2, 3, 5, 4};
    for (int k = 0; k < 6; ++k)
        hoverAt(burstDx[k], burstDy[k]);
    CHECK(renderCount() == burstBase, "M30(a): the hover burst performs no synchronous render");
    pump(60);
    CHECK(renderCount() == burstBase + 1,
          "M30(a): a hover burst renders the inspector exactly once");
    QLabel *coord = ws->findChild<QLabel *>("pixelInspectorCoordLabel");
    if (coord)
    {
        const QPointF lastImg = view0->widgetToImage(view0->rect().center() + QPoint(7, 4));
        const QString expectedLast = QStringLiteral("(%1, %2)")
                                         .arg(static_cast<int>(std::floor(lastImg.x())))
                                         .arg(static_cast<int>(std::floor(lastImg.y())));
        CHECK(coord->text() == expectedLast,
              "M30(a): the single render uses the latest hover coordinate");
    }
    else
    {
        CHECK(false, "M30(a): coordinate label is discoverable");
    }

    // (b) One physical hover with the synced crosshair ON emits both pixelInfo
    // and crosshairMoved; the pair must render the inspector exactly once.
    QCheckBox *crosshair = findChk(ws, QStringLiteral("同步准星"));
    CHECK(crosshair != nullptr, "M30: synced crosshair toggle is discoverable");
    if (crosshair)
    {
        const quint64 beforeToggle = renderCount();
        crosshair->setChecked(true);
        pump(20);
        CHECK(renderCount() == beforeToggle,
              "M30(b): enabling the crosshair alone does not render");
        const quint64 beforePair = renderCount();
        hoverAt(4, 4);
        pump(60);
        CHECK(renderCount() == beforePair + 1,
              "M30(b): the pixelInfo + crosshairMoved pair renders once, not twice");
        crosshair->setChecked(false);
        pump(20);
    }

    // (c) Ordinary re-renders reuse the existing QTableWidgetItem objects.
    {
        const QTableWidgetItem *cell00 = inspector->item(0, 0);
        const QTableWidgetItem *cell12 = inspector->item(1, 2);
        CHECK(cell00 && cell12 && !cell12->text().isEmpty(),
              "M30(c): inspector cells are populated before the re-render");
        const quint64 before = renderCount();
        hoverAt(1, 1);
        pump(60);
        CHECK(renderCount() == before + 1, "M30(c): a new hover re-renders");
        CHECK(inspector->item(0, 0) == cell00 && inspector->item(1, 2) == cell12,
              "M30(c): table item objects are reused across ordinary renders");
    }

    // (d) Semantic control changes refresh the current coordinate correctly.
    QComboBox *csCombo = ws->findChild<QComboBox *>("pixelInspectorColorSpaceCombo");
    QComboBox *kernelCombo = ws->findChild<QComboBox *>("pixelInspectorKernelCombo");
    QLabel *statsLabel = ws->findChild<QLabel *>("pixelInspectorStatsLabel");
    CHECK(csCombo && kernelCombo && statsLabel,
          "M30: color-space / kernel / stats controls are discoverable");
    if (csCombo)
    {
        const QString beforeText = inspector->item(1, 2)->text();
        csCombo->setCurrentIndex(2); // HSV
        pump(60);
        const QString hsvText = inspector->item(1, 2)->text();
        bool numeric = false;
        hsvText.toDouble(&numeric);
        CHECK(hsvText != beforeText && numeric,
              "M30(d): color-space change refreshes the current pixel readout");
        CHECK(inspector->horizontalHeaderItem(2) &&
                  inspector->horizontalHeaderItem(2)->text() == QStringLiteral("H"),
              "M30(d): header labels follow the selected color space");
        csCombo->setCurrentIndex(0); // RGB
        pump(60);
        CHECK(inspector->item(1, 2)->text() == beforeText,
              "M30(d): returning to RGB restores the original readout");
    }
    if (kernelCombo && statsLabel)
    {
        const QString beforeStats = statsLabel->text();
        CHECK(!beforeStats.isEmpty(), "M30(d): stats label is populated at the hover point");
        kernelCombo->setCurrentIndex(2); // 5×5
        pump(60);
        const QString afterStats = statsLabel->text();
        CHECK(afterStats != beforeStats && afterStats.contains(QStringLiteral("5×5")),
              "M30(d): kernel change refreshes the neighborhood stats");
        kernelCombo->setCurrentIndex(1); // back to 3×3
        pump(60);
    }
    {
        // Focus/reference change via the real pane double-click path (M30
        // review): RawImageView::mouseDoubleClickEvent emits focusRequested, and
        // onFocusRequested must lock the double-clicked pane as the reference
        // exactly once — the button is synchronized without re-entering through
        // its toggled handler and immediately toggling the lock back off.
        const QPoint hovPt = view1->rect().center();
        QMouseEvent hov(QEvent::MouseMove, QPointF(hovPt), Qt::NoButton, Qt::NoButton,
                        Qt::NoModifier);
        QApplication::sendEvent(view1, &hov);
        pump(60);
        QPushButton *lockReference = nullptr;
        for (QPushButton *b : ws->findChildren<QPushButton *>())
            if (b->isCheckable() && b->text().startsWith(QStringLiteral("锁定基准")))
                lockReference = b;
        CHECK(lockReference != nullptr, "M30: lock-reference button is discoverable");
        if (!lockReference)
            return;
        auto doubleClick = [](RawImageView *v, const QPoint &pt)
        {
            QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(pt), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &event);
        };
        // First double-click locks pane 1 (m_focusIndex 1, button checked, and
        // the inspector refreshes with a zero delta on the reference row).
        const quint64 before = renderCount();
        doubleClick(view1, hovPt);
        pump(60);
        CHECK(renderCount() > before,
              "M30(d): pane double-click re-renders the inspector at the current coordinate");
        CHECK(lockReference->isChecked(), "M30(d): pane double-click locks the reference button");
        CHECK(ws->focusImagePath() == b, "M30(d): the workspace reference path is the locked pane");
        CHECK(sel.focused() == b,
              "M30(d): the SelectionModel focused path follows the locked reference");
        CHECK(inspector->item(1, 5) && inspector->item(1, 5)->text() == QStringLiteral("0"),
              "M30(d): the locked reference pane shows a zero delta");
        CHECK(inspector->rowCount() == 2 && inspector->item(0, 2) &&
                  !inspector->item(0, 2)->text().isEmpty(),
              "M30(d): the inspector stays fully populated after the focus change");
        // Second double-click on the same pane unlocks it and the button follows.
        doubleClick(view1, hovPt);
        pump(60);
        CHECK(!lockReference->isChecked(),
              "M30(d): double-clicking the locked pane again unlocks the reference");
        CHECK(sel.focused().isEmpty(), "M30(d): unlocking clears the SelectionModel focused path");
    }

    // (e) A queued render must be lifetime-safe: destroying the workspace with
    // a pending inspector render never acts on the destroyed object.
    {
        auto *sub = new QWidget;
        auto *subLay = new QVBoxLayout(sub);
        auto *subWs = new CompareWorkspace(sub);
        subLay->addWidget(subWs);
        SelectionModel subSel;
        subWs->setSelectionModel(&subSel);
        subWs->setImages({a, b});
        sub->resize(900, 600);
        sub->show();
        waitForCompareCount(subWs, 2);
        QCheckBox *subToggle = subWs->findChild<QCheckBox *>("analysisPanelToggle");
        RawImageView *subView = nullptr;
        for (RawImageView *v : subWs->findChildren<RawImageView *>())
            if (v && v->cellIndex() == 0)
                subView = v;
        if (subToggle && subView)
        {
            subToggle->setChecked(true);
            pump(30);
            {
                QElapsedTimer t;
                t.start();
                while (subView->image().isNull() && t.elapsed() < 8000)
                    pump(25);
            }
            const QPoint pt = subView->rect().center();
            QMouseEvent event(QEvent::MouseMove, QPointF(pt), Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(subView, &event); // schedules a queued render
            QPointer<CompareWorkspace> destroyed = subWs;
            delete sub; // destroys subWs while the queued render is still pending
            CHECK(destroyed.isNull(), "M30(e): workspace is destroyed before queued render drains");
        }
        else
        {
            delete sub;
        }
        pump(80); // the dropped queued render must not touch the destroyed workspace
        CHECK(QApplication::closingDown() == false,
              "M30(e): pending inspector render does not close the application");
    }
}

void testCanvasDoubleClickGestures(const QString &a, const QString &b)
{
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({a, b});
    dlg.resize(800, 600);
    dlg.show();
    waitForCompareCount(ws, 2);
    pump(100);

    auto *swipe = findChk(ws, QStringLiteral("滑动对比"));
    CHECK(swipe && swipe->isEnabled(), "Canvas double-click: swipe mode is available");
    if (!swipe)
        return;
    swipe->setChecked(true);
    pump(50);

    auto *canvas = ws->findChild<QWidget *>(QStringLiteral("compareCanvas"));
    CHECK(canvas && canvas->isVisible(),
          "Canvas double-click: compareCanvas is visible in swipe mode");
    if (!canvas)
        return;

    // Drag the divider to 0.25
    const int startX = int(canvas->width() * 0.5);
    const int targetX = int(canvas->width() * 0.25);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(startX, 300), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(targetX, 300), Qt::LeftButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(targetX, 300), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    pump(30);

    // Double click at targetX should reset divider to 0.5
    QMouseEvent dblDivider(QEvent::MouseButtonDblClick, QPointF(targetX, 300), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &dblDivider);
    pump(30);

    // Double click away from divider (e.g. at 0.8) should toggle zoom
    const double initialScale = ws->engine().cellTransform(0).scale;
    QMouseEvent dblZoom(QEvent::MouseButtonDblClick, QPointF(canvas->width() * 0.8, 300),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &dblZoom);
    pump(30);
    const double zoomedScale = ws->engine().cellTransform(0).scale;
    CHECK(std::abs(zoomedScale - initialScale) > 0.001,
          "Canvas double-click: double clicking canvas toggles zoom from fit");

    // Double clicking again should restore fit
    QApplication::sendEvent(canvas, &dblZoom);
    pump(30);
    const double restoredScale = ws->engine().cellTransform(0).scale;
    CHECK(std::abs(restoredScale - initialScale) < 0.01,
          "Canvas double-click: double clicking again restores fit");
}

void testSyncRotate(const QString &img0, const QString &img1)
{
    std::cout << "── Compare: Synchronized rotation across cells ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({img0, img1});
    dlg.resize(900, 600);
    dlg.show();
    waitForCompareCount(ws, 2);

    CHECK(!ws->syncRotate(), "Sync rotate is initially off by default");
    CHECK(ws->cellRotation(0) == 0 && ws->cellRotation(1) == 0, "Initial cell rotations are 0");

    // Unsynchronized: rotating pane 0 only rotates pane 0.
    ws->setEditCellIndex(0);
    ws->rotateCurrentCell(90);
    pump(30);
    CHECK(ws->cellRotation(0) == 90 && ws->cellRotation(1) == 0,
          "Single-pane rotate only modifies the active pane when syncRotate is off");

    // Turn on sync rotate via API / checkbox.
    ws->setSyncRotate(true);
    CHECK(ws->syncRotate(), "Sync rotate is now on");
    auto *tbChk = ws->findChild<QCheckBox *>("syncRotateCheck");
    CHECK(tbChk && tbChk->isChecked(), "Toolbar sync rotate checkbox is checked");
    auto *pnlChk = ws->findChild<QCheckBox *>("syncRotatePanelCheck");
    CHECK(pnlChk && pnlChk->isChecked(), "Edit panel sync rotate checkbox is checked");

    // Rotating by 90 synchronously rotates all panes.
    ws->rotateCurrentCell(90);
    pump(30);
    CHECK(ws->cellRotation(0) == 180 && ws->cellRotation(1) == 90,
          "Both panes rotate synchronously by 90 degrees when syncRotate is on");

    // Rotating by -90 synchronously rotates all panes.
    ws->rotateCurrentCell(-90);
    pump(30);
    CHECK(ws->cellRotation(0) == 90 && ws->cellRotation(1) == 0,
          "Both panes rotate synchronously by -90 degrees");

    // Synchronous flip.
    CHECK(!ws->cellFlipH(0) && !ws->cellFlipH(1), "Initial flipH is false");
    ws->flipCurrentCell(true);
    pump(30);
    CHECK(ws->cellFlipH(0) && ws->cellFlipH(1), "Both panes flip horizontally together");

    // Turn off sync rotate via panel checkbox.
    pnlChk->setChecked(false);
    pump(30);
    CHECK(!ws->syncRotate(), "Unchecking panel checkbox turns off sync rotate");
    CHECK(tbChk && !tbChk->isChecked(), "Toolbar checkbox synchronizes to unchecked");

    ws->setEditCellIndex(1);
    ws->rotateCurrentCell(90);
    pump(30);
    CHECK(ws->cellRotation(0) == 90 && ws->cellRotation(1) == 90,
          "Rotating pane 1 only modifies pane 1 when syncRotate is off");
}

void testCompareShortcutHelpSnapshot(const QDir &dir, const QString &img0, const QString &img1)
{
    std::cout << "── Compare: shortcut table, help, snapshot, auto threshold ──\n";
    bool rowsFilled = true;
    for (const mviewer::cw::CompareShortcut &entry : mviewer::cw::kCompareShortcuts)
    {
        if (!entry.keys || entry.keys[0] == '\0' || !entry.name || entry.name[0] == '\0')
            rowsFilled = false;
    }
    CHECK(rowsFilled, "every compare shortcut has a non-empty name and keys");

    const char *requiredKeys[] = {"W", "I", "Alt+R",        "Ctrl+R", "Ctrl+Shift+R",
                                  "[", ",", "Ctrl+Shift+C", "Ctrl+C", "Ctrl+S"};
    bool requiredPresent = true;
    for (const char *keys : requiredKeys)
    {
        if (!mviewer::cw::findCompareShortcut(keys))
            requiredPresent = false;
    }
    CHECK(requiredPresent, "shortcut table lists W, I, rotate, threshold, copy, and save");

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1100, 750);
    dlg.show();
    pump(30);

    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动对比"));
    const mviewer::cw::CompareShortcut *swipeKeys = mviewer::cw::findCompareShortcut("W");
    QString swipeName;
    if (swipe)
    {
        const qsizetype cut = swipe->text().indexOf(QLatin1Char('('));
        swipeName = (cut < 0) ? swipe->text().trimmed() : swipe->text().left(cut).trimmed();
    }
    const bool nameMatches =
        swipeKeys && swipeKeys->name && swipeName == QString::fromUtf8(swipeKeys->name);
    CHECK(nameMatches, "W shortcut name matches the swipe checkbox text");

    const QString titleBefore = dlg.windowTitle();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Question, Qt::NoModifier);
    QApplication::sendEvent(ws, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Question, Qt::NoModifier);
    QApplication::sendEvent(ws, &release);
    pump(30);
    auto *help = ws->findChild<QDialog *>(QStringLiteral("compareShortcutHelpDialog"));
    auto *table =
        help ? help->findChild<QTableWidget *>(QStringLiteral("compareShortcutTable")) : nullptr;
    const int shortcutCount = static_cast<int>(std::size(mviewer::cw::kCompareShortcuts));
    CHECK(help && help->isVisible(), "? shows the compare shortcut help dialog");
    CHECK(dlg.windowTitle() == titleBefore, "? keeps the compare window title");
    CHECK(table && table->rowCount() == shortcutCount, "shortcut help rows match the table");

    QKeyEvent pressAgain(QEvent::KeyPress, Qt::Key_Question, Qt::NoModifier);
    QApplication::sendEvent(ws, &pressAgain);
    QKeyEvent releaseAgain(QEvent::KeyRelease, Qt::Key_Question, Qt::NoModifier);
    QApplication::sendEvent(ws, &releaseAgain);
    pump(20);
    CHECK(help && !help->isVisible(), "? hides the compare shortcut help dialog");

    auto *snapshot = ws->findChild<QToolButton *>(QStringLiteral("compareSnapshotButton"));
    CHECK(snapshot, "compare snapshot button exists");
    CHECK(ws->findChild<QAction *>(QStringLiteral("compareSnapshotCopyAction")),
          "snapshot menu copies the view");
    CHECK(ws->findChild<QAction *>(QStringLiteral("compareSnapshotSaveAction")),
          "snapshot menu saves a png");

    ws->setImages({img0, img1});
    waitForCompareCount(ws, 2);
    const QString snapPath = dir.filePath(QStringLiteral("compare_snap.png"));
    CHECK(!ws->saveComparisonViewTo(QString()), "saveComparisonViewTo rejects an empty path");
    CHECK(ws->saveComparisonViewTo(snapPath), "saveComparisonViewTo writes the compare view");
    CHECK(QFile::exists(snapPath), "snapshot png exists");
    const QImage saved(snapPath);
    CHECK(!saved.isNull(), "snapshot png decodes");

    auto *diffToggle = ws->findChild<QCheckBox *>(QStringLiteral("diffOverlayToggle"));
    auto *autoBtn = ws->findChild<QPushButton *>(QStringLiteral("diffAutoThresholdButton"));
    CHECK(diffToggle && autoBtn, "diff overlay and auto-threshold controls exist");
    if (diffToggle && autoBtn)
    {
        diffToggle->setChecked(true);
        // setImages drops the previous suggestion before the next diff result.
        ws->setImages({img1, img0});
        CHECK(ws->comparedImageCount() == 2, "loaded panes stay up while the next pair loads");
        CHECK(diffToggle->isChecked(), "diff stays on when the compared set changes");
        CHECK(!autoBtn->isEnabled(), "auto threshold is disabled before the new diff result");
    }
}

void testComparePresetSanitize()
{
    std::cout << "── Compare: preset value clamps ──\n";
    CHECK(mviewer::cw::clampRotation(45) == 0, "rotation 45 snaps to 0");
    CHECK(mviewer::cw::clampRotation(-90) == 270, "rotation -90 normalizes to 270");
    CHECK(mviewer::cw::clampRotation(450) == 90, "rotation 450 snaps to 90");
    CHECK(mviewer::cw::clampThreshold(300) == 255, "threshold 300 clamps to 255");
    CHECK(mviewer::cw::clampThreshold(-5) == 0, "threshold -5 clamps to 0");
    CHECK(mviewer::cw::clampBlinkMs(1) == 50, "blink 1 clamps to 50 ms");
    CHECK(mviewer::cw::clampBlinkMs(99999) == 5000, "blink 99999 clamps to 5000 ms");
    CHECK(mviewer::cw::clampColumns(0) == 1, "columns 0 clamps to 1");
    CHECK(mviewer::cw::clampColumns(50) == 8, "columns 50 clamps to 8");
    const mviewer::cw::PresetCrop crop = mviewer::cw::clampCrop(-4, -2, -9, 12);
    CHECK(crop.x == 0 && crop.y == 0 && crop.w == 0 && crop.h == 12,
          "negative crop edges clamp to zero");
    CHECK(mviewer::cw::clampBrightness(-400) == -255, "brightness below range clamps to -255");
    CHECK(mviewer::cw::clampBrightness(400) == 255, "brightness above range clamps to 255");
    CHECK(mviewer::cw::clampBrightness(-255) == -255, "brightness at the low bound stays");
    CHECK(mviewer::cw::clampGamma(0.0f) == 0.05f, "gamma below range clamps to 0.05");
    CHECK(mviewer::cw::clampGamma(9.0f) == 8.0f, "gamma above range clamps to 8");
    CHECK(mviewer::cw::clampGamma(1.0f) == 1.0f, "gamma identity stays");
    CHECK(mviewer::cw::clampSharedScale(0.0) == 0.01, "sharedScale below range clamps to 0.01");
    CHECK(mviewer::cw::clampSharedScale(80.0) == 50.0, "sharedScale above range clamps to 50");
    CHECK(mviewer::cw::clampSharedScale(1.0) == 1.0, "sharedScale identity stays");
    CHECK(mviewer::cw::clampContrast(-1.0f) == 0.0f, "contrast below range clamps to 0");
    CHECK(mviewer::cw::clampContrast(4.0f) == 3.0f, "contrast above range clamps to 3");
    CHECK(mviewer::cw::clampGain(0.0f) == 0.01f, "gain below range clamps to 0.01");
    CHECK(mviewer::cw::clampGain(9.0f) == 5.0f, "gain above range clamps to 5");
}

struct LayoutMarks
{
    int count = 0;
    int nav = 0;
    int forcedCols = -1;
    int combo = -1;
};

QComboBox *compareLayoutCombo(QWidget *root)
{
    const auto boxes = root->findChildren<QComboBox *>();
    for (QComboBox *box : boxes)
    {
        if (box && box->count() > 0 && box->itemText(0) == QStringLiteral("自动"))
            return box;
    }
    return nullptr;
}

LayoutMarks layoutMarks(CompareWorkspace *ws)
{
    LayoutMarks marks;
    marks.count = ws->comparedImageCount();
    marks.nav = ws->navWindow();
    marks.forcedCols = ws->engine().forcedColumns();
    if (QComboBox *combo = compareLayoutCombo(ws))
        marks.combo = combo->currentIndex();
    return marks;
}

bool sameLayout(const LayoutMarks &before, const LayoutMarks &after)
{
    return before.count == after.count && before.nav == after.nav &&
           before.forcedCols == after.forcedCols && before.combo == after.combo;
}

RawImageView *comparePaneView(QWidget *root, int index)
{
    QWidget *pane = root->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(index));
    return pane ? pane->findChild<RawImageView *>() : nullptr;
}

void tapCompareKey(QWidget *target, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(target, &release);
}

bool waitForStableCompareScale(CompareWorkspace *ws, int count, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    double previous = -1.0;
    while (timer.elapsed() < timeoutMs)
    {
        pump(40);
        bool imagesReady = ws->comparedImageCount() == count;
        for (int i = 0; imagesReady && i < count; ++i)
        {
            RawImageView *view = comparePaneView(ws, i);
            if (!view || view->displayImage().isNull() || view->width() < 16)
                imagesReady = false;
        }
        const double scale = imagesReady ? ws->engine().cellTransform(0).scale : 0.0;
        if (imagesReady && scale > 0.0 && std::abs(scale - previous) < 1e-9)
            return true;
        previous = scale;
    }
    return false;
}

double nudgeCompareZoom(CompareWorkspace *ws)
{
    tapCompareKey(ws, Qt::Key_Equal);
    tapCompareKey(ws, Qt::Key_Equal);
    pump(30);
    return ws->engine().cellTransform(0).scale;
}

void pointAtComparePane(RawImageView *view)
{
    if (!view)
        return;
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(view, &enter);
    QCursor::setPos(view->mapToGlobal(view->rect().center()));
}

// Plain 1–8 preview images. They must not zoom to 100% or apply a layout preset.
// Ctrl+1 remains actual size. Ctrl+2 remains the 2-up preset.
void testPlainDigitsReservedForPreview(const QDir &dir)
{
    std::cout << "── Compare: plain digits preview images ──\n";
    bool plainOneIsZoom = false;
    bool digitsChangeLayout = false;
    bool ctrl1IsActual = false;
    bool digitsPreview = false;
    for (const mviewer::cw::CompareShortcut &entry : mviewer::cw::kCompareShortcuts)
    {
        const QString keys = QString::fromUtf8(entry.keys ? entry.keys : "");
        const QString text = QString::fromUtf8(entry.name ? entry.name : "") +
                             QString::fromUtf8(entry.description ? entry.description : "");
        if (keys == QLatin1String("1") && text.contains(QStringLiteral("实际大小")))
            plainOneIsZoom = true;
        if (text.contains(QStringLiteral("套用宫格")) || text.contains(QStringLiteral("改布局")))
            digitsChangeLayout = true;
        if (keys == QLatin1String("Ctrl+1") && text.contains(QStringLiteral("100%")))
            ctrl1IsActual = true;
        if (keys.contains(QLatin1Char('1')) && keys.contains(QLatin1Char('8')) &&
            text.contains(QStringLiteral("临时")))
            digitsPreview = true;
    }
    CHECK(!plainOneIsZoom && ctrl1IsActual && digitsPreview && !digitsChangeLayout,
          "shortcut table reserves plain 1-8 for preview and Ctrl+1 for 100%");

    const QString red =
        writePng(dir, QStringLiteral("digit_r.png"), 1200, 900, QColor(220, 20, 20));
    const QString green =
        writePng(dir, QStringLiteral("digit_g.png"), 1200, 900, QColor(20, 200, 40));
    const QString blue =
        writePng(dir, QStringLiteral("digit_b.png"), 1200, 900, QColor(20, 40, 220));

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1000, 700);
    dlg.show();
    pump(30);

    ws->setImages({red, green});
    CHECK(waitForStableCompareScale(ws, 2, 60000), "two-image digit fixture is displayed");
    const double twoScale = nudgeCompareZoom(ws);
    CHECK(std::abs(twoScale - 1.0) > 0.05, "two-image zoom is away from 100% before digits");
    const LayoutMarks twoLayout = layoutMarks(ws);
    tapCompareKey(ws, Qt::Key_2);
    tapCompareKey(ws, Qt::Key_3);
    pump(40);
    const LayoutMarks twoAfter = layoutMarks(ws);
    CHECK(sameLayout(twoLayout, twoAfter), "plain 2/3 with two images do not change the layout");
    CHECK(std::abs(ws->engine().cellTransform(0).scale - twoScale) < 1e-6,
          "plain 2/3 with two images do not change zoom");
    tapCompareKey(ws, Qt::Key_1);
    pump(20);
    CHECK(std::abs(ws->engine().cellTransform(0).scale - twoScale) < 1e-6,
          "plain 1 with two images does not set 100% zoom");
    CHECK(sameLayout(twoLayout, layoutMarks(ws)), "plain 1 with two images does not change layout");

    ws->setImages({red, green, blue});
    CHECK(waitForStableCompareScale(ws, 3, 60000), "three-image digit fixture is displayed");
    const double threeScale = nudgeCompareZoom(ws);
    CHECK(std::abs(threeScale - 1.0) > 0.05, "three-image zoom is away from 100% before digit 1");
    QCursor::setPos(-10000, -10000);
    const LayoutMarks threeLayout = layoutMarks(ws);
    tapCompareKey(ws, Qt::Key_2);
    tapCompareKey(ws, Qt::Key_3);
    tapCompareKey(ws, Qt::Key_4);
    pump(40);
    CHECK(sameLayout(threeLayout, layoutMarks(ws)),
          "plain 2/3/4 with three images do not change the layout preset");
    CHECK(std::abs(ws->engine().cellTransform(0).scale - threeScale) < 1e-6,
          "plain 2/3/4 with three images do not change zoom");
    auto *status = ws->findChild<QLabel *>(QStringLiteral("compareStatusLabel"));
    CHECK(status && status->text().contains(QStringLiteral("当前共 3 张")),
          "a digit past the image count shows a status hint");

    RawImageView *pane0 = comparePaneView(ws, 0);
    RawImageView *pane2 = comparePaneView(ws, 2);
    CHECK(pane0 && pane2 && !pane0->displayImage().isNull() && !pane2->displayImage().isNull(),
          "pane 0 and pane 2 have images before digit 1");
    if (!pane0 || !pane2 || pane0->displayImage().isNull() || pane2->displayImage().isNull())
        return;
    const QRgb image1 = pane0->displayImage().pixel(0, 0);
    const QRgb image3 = pane2->displayImage().pixel(0, 0);
    CHECK(image1 != image3, "image 1 and image 3 are visually distinct");
    pointAtComparePane(pane2);
    QKeyEvent press1(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
    QApplication::sendEvent(ws, &press1);
    pump(20);
    CHECK(pane2->hasTransientDisplay(), "plain 1 with three images previews on the target pane");
    CHECK(pane2->displayImage().pixel(0, 0) == image1,
          "plain 1 with three images previews image 1");
    CHECK(!pane0->hasTransientDisplay(), "plain 1 does not replace the pane that already shows it");
    CHECK(std::abs(ws->engine().cellTransform(0).scale - threeScale) < 1e-6,
          "plain 1 with three images does not change zoom");
    CHECK(sameLayout(threeLayout, layoutMarks(ws)), "plain 1 does not change the layout preset");
    QKeyEvent release1(QEvent::KeyRelease, Qt::Key_1, Qt::NoModifier);
    QApplication::sendEvent(ws, &release1);
    pump(20);
    CHECK(!pane2->hasTransientDisplay(), "releasing 1 restores the pane");

    tapCompareKey(ws, Qt::Key_1, Qt::ControlModifier);
    pump(30);
    CHECK(std::abs(ws->engine().cellTransform(0).scale - 1.0) < 0.02, "Ctrl+1 sets 100% zoom");
    CHECK(ws->comparedImageCount() == 3, "Ctrl+1 keeps the three images");
    CHECK(sameLayout(threeLayout, layoutMarks(ws)), "Ctrl+1 does not change the layout preset");

    tapCompareKey(ws, Qt::Key_2, Qt::ControlModifier);
    waitForCompareCount(ws, 2);
    CHECK(ws->comparedImageCount() == 2 && ws->navWindow() == 2 &&
              ws->engine().forcedColumns() == 2,
          "Ctrl+2 still applies the 2-up layout preset");
    QComboBox *combo = compareLayoutCombo(ws);
    CHECK(combo && combo->currentIndex() == 2, "Ctrl+2 selects the 2-column layout");
}

} // namespace
