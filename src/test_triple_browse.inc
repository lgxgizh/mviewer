// Three independent directory panes; keep this in the existing browse acceptance gate.
void testTripleSelectionModel()
{
    using Pane = SelectionModel::PaneSlot;
    SelectionModel model;
    model.setSelection({"left"}, "left");
    model.setBrowsePaneCount(3);
    model.setPaneSelection(Pane::Secondary, {"middle", "shared"}, "middle");
    model.setActivePane(Pane::Tertiary);
    model.setPaneSelection(Pane::Tertiary, {"right", "shared"}, "right");
    EXPECT(model.selection() == QStringList({"left", "middle", "shared", "right"}),
           "three-pane union de-duplicates paths in pane order");
    EXPECT(model.currentImage() == "right", "third pane owns the current image");
    model.setBrowsePaneCount(2);
    EXPECT(model.paneSelection(Pane::Tertiary).isEmpty(), "closing third clears its selection");
    EXPECT(model.selection() == QStringList({"left", "middle", "shared"}),
           "returning to two panes preserves both original selections");
    model.setPaneSelection(Pane::Tertiary, {"hidden"});
    EXPECT(!model.selection().contains("hidden"), "hidden third cannot pollute two-pane union");
    model.setBrowsePaneCount(1);
    EXPECT(model.selection() == QStringList({"left"}), "single mode preserves primary selection");
    model.setPaneSelection(Pane::Secondary, {"hidden"});
    EXPECT(model.selection() == QStringList({"left"}), "hidden panes cannot overwrite primary");
}

void testTripleBrowse(const QString &leftDir, const QString &middleDir, const QString &thirdDir)
{
    using Pane = SelectionModel::PaneSlot;
    AppState().save();
    MainWindow mw;
    mw.resize(1800, 900);
    mw.show();
    QApplication::processEvents();
    mw.openExternalTargets({leftDir});
    auto *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    EXPECT(waitUntil([&]() { return left && left->pathList().size() >= 2; }, 160),
           "triple fixture primary directory loaded");
    auto *doubleAction = mw.findChild<QAction *>(QStringLiteral("splitBrowseAction"));
    auto *tripleAction = mw.findChild<QAction *>(QStringLiteral("tripleBrowseAction"));
    auto *toolbar = mw.findChild<QToolBar *>(QStringLiteral("browserToolBar"));
    EXPECT(tripleAction && toolbar && toolbar->actions().contains(tripleAction) &&
               !tripleAction->icon().isNull(),
           "independent three-column toolbar button has icon");
    if (!left || !doubleAction || !tripleAction)
        return;
    left->setSort(ThumbnailPanel::SortName, false);
    tripleAction->trigger();
    auto *middle = mw.secondaryThumbnailPanel();
    auto *third = mw.tertiaryThumbnailPanel();
    EXPECT(mw.browseColumnCount() == 3 && tripleAction->isChecked() && !doubleAction->isChecked(),
           "three-column action and double action are exclusive");
    EXPECT(waitUntil(
               [&]()
               {
                   return middle && third && middle->pathList().size() >= 2 &&
                          third->pathList().size() >= 2;
               },
               160),
           "new panes mirror the primary directory");
    if (!middle || !third)
        return;
    EXPECT(third->currentDir() == left->currentDir() && third->sortMode() == left->sortMode() &&
               third->sortAscending() == left->sortAscending(),
           "third mirrors left on entry");
    EXPECT(left->consumerId() != third->consumerId() && middle->consumerId() != third->consumerId(),
           "third has an isolated thumbnail consumer");
    mw.openRightDirectory(middleDir);
    mw.openThirdDirectory(thirdDir);
    EXPECT(waitUntil(
               [&]()
               {
                   return middle->pathList().size() >= 2 && third->pathList().size() >= 2 &&
                          third->currentDir() == thirdDir;
               },
               160),
           "three independent directories loaded");
    EXPECT(left->currentDir() == leftDir && middle->currentDir() == middleDir,
           "opening third leaves first and second directories unchanged");
    EXPECT(mw.activeThumbnailPanel() == third && third->currentDir() == thirdDir,
           "toolbar directory routing follows third pane");
    third->setSort(ThumbnailPanel::SortSize, true);
    EXPECT(!left->sortAscending(), "third sort remains independent");
    left->setThumbSize(120);
    left->setViewMode(ThumbnailPanel::Details);
    EXPECT(third->thumbSize() == left->thumbSize() &&
               third->viewMode() == ThumbnailPanel::Details &&
               middle->viewMode() == ThumbnailPanel::Details,
           "size and Details sync across panes");
    bool actionOwnsCtrl3 = false;
    for (QAction *action : mw.findChildren<QAction *>())
        if (action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_3))
            actionOwnsCtrl3 = true;
    left->setViewMode(ThumbnailPanel::Thumbnail);
    QKeyEvent detailsKey(QEvent::KeyPress, Qt::Key_3, Qt::ControlModifier);
    QApplication::sendEvent(&mw, &detailsKey);
    EXPECT(!actionOwnsCtrl3 && left->viewMode() == ThumbnailPanel::Details &&
               tripleAction->isChecked() && mw.browseColumnCount() == 3,
           "Ctrl+3 remains Details, not triple browse");
    left->selectPath(left->pathList().first());
    middle->selectPath(middle->pathList().first());
    third->selectPath(third->pathList().first());
    EXPECT(third->compareSelection().size() == 3 && mw.compareAction()->isEnabled(),
           "Compare reads the three-pane union");
    EXPECT(third->property("otherPaneDirectories").toStringList().contains(leftDir) &&
               third->property("otherPaneDirectories").toStringList().contains(middleDir),
           "third can transfer files to either original pane");
    auto *path = mw.findChild<QLineEdit *>(QStringLiteral("tertiaryPathEdit"));
    EXPECT(path && !path->placeholderText().isEmpty(), "third has an independent address bar");
    if (path)
    {
        path->setText(leftDir);
        QMetaObject::invokeMethod(path, "returnPressed", Qt::DirectConnection);
        EXPECT(third->currentDir() == leftDir && middle->currentDir() == middleDir,
               "third address navigation is independent");
        // Keyboard history routing is pane-local, including while its editor is focused.
        QKeyEvent backKey(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier);
        QApplication::sendEvent(third->viewport(), &backKey);
        EXPECT(third->currentDir() == thirdDir, "third Alt+Left restores its own history");
    }
    mw.setActiveBrowsePane(Pane::Primary);
    doubleAction->trigger();
    EXPECT(mw.browseColumnCount() == 2 && doubleAction->isChecked() && !tripleAction->isChecked(),
           "double button returns to the original two-column mode");
    EXPECT(middle->currentDir() == middleDir, "returning to two columns keeps middle directory");
    EXPECT(left->selectedPaths().size() == 1 && middle->selectedPaths().size() == 1,
           "returning to two columns preserves both selections");
    doubleAction->trigger();
    EXPECT(mw.browseColumnCount() == 1 && mw.activeThumbnailPanel() == left,
           "single-column mode still works after triple mode");
}

void testTriplePersistence(const QString &leftDir, const QString &middleDir,
                           const QString &thirdDir)
{
    AppState saved;
    saved.lastDir = leftDir;
    saved.splitBrowseEnabled = true;
    saved.tripleBrowseEnabled = true;
    saved.secondaryLastDir = middleDir;
    saved.tertiaryLastDir = thirdDir;
    saved.tertiaryDirHistory = {leftDir, thirdDir};
    saved.tertiaryDirHistoryIndex = 1;
    saved.hasTertiarySort = true;
    saved.tertiarySortField = static_cast<int>(ThumbnailPanel::SortSize);
    saved.tertiarySortAscending = false;
    saved.gallerySplitterSizes = {400, 400, 400};
    saved.tertiaryLastImage = QDir(thirdDir).filePath("t_a.png");
    saved.tertiaryThumbScroll = 42;
    EXPECT(saved.save(), "triple state saved");
    const AppState loaded = AppState::load();
    EXPECT(loaded.tripleBrowseEnabled && loaded.tertiaryLastDir == thirdDir &&
               loaded.tertiaryDirHistory == saved.tertiaryDirHistory &&
               loaded.tertiaryDirHistoryIndex == 1 && loaded.tertiaryThumbScroll == 42 &&
               loaded.tertiaryLastImage == saved.tertiaryLastImage && loaded.hasTertiarySort &&
               !loaded.tertiarySortAscending,
           "triple path/history/position/sort round-trip");
    {
        MainWindow restored;
        restored.resize(1800, 900);
        restored.show();
        EXPECT(waitUntil(
                   [&]()
                   {
                       return restored.browseColumnCount() == 3 &&
                              restored.tertiaryThumbnailPanel()->currentDir() == thirdDir &&
                              !restored.tertiaryThumbnailPanel()->pathList().isEmpty();
                   },
                   160),
               "session restores three distinct directories");
        EXPECT(restored.secondaryThumbnailPanel()->currentDir() == middleDir &&
                   restored.tertiaryThumbnailPanel()->sortMode() == ThumbnailPanel::SortSize &&
                   !restored.tertiaryThumbnailPanel()->sortAscending(),
               "session does not mirror over the saved third directory or sort");
    }
    AppState().save();
}
