// Included by test_split_browse_acceptance.cpp (inside its anonymous
// namespace): entering split browse mirrors the single pane, TC-SB-18.

bool sameDirPath(const QString &a, const QString &b)
{
    return !a.isEmpty() && !b.isEmpty() &&
           (QDir::cleanPath(a) == QDir::cleanPath(b) || QDir(a) == QDir(b));
}

// Regression: the split button reopened the right pane on a stale folder
// (the right pane's previous or saved folder) instead of "one cell splitting
// into two" with the same contents as the single pane.
void testSplitMirrorsSinglePane(const QString &leftDir, const QString &rightDir,
                                const QString &thirdDir)
{
    std::cout << "--- TC-SB-18: split button mirrors the single pane ---" << std::endl;
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    auto *splitAct = mw.findChild<QAction *>(QStringLiteral("splitBrowseAction"));
    auto *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    ThumbnailPanel *right = mw.secondaryThumbnailPanel();
    auto *rightPath = mw.findChild<QLineEdit *>(QStringLiteral("secondaryPathEdit"));
    EXPECT(splitAct && left && right && rightPath, "split action and both panes exist");
    if (!splitAct || !left || !right || !rightPath)
        return;

    // A stale right pane from an earlier split: it shows rightDir, sorted by
    // name, and that folder is what the session would remember.
    mw.openExternalTargets({leftDir});
    if (!splitAct->isChecked())
        splitAct->trigger();
    mw.openRightDirectory(rightDir);
    right->setSort(ThumbnailPanel::SortName, true);
    EXPECT(waitUntil([&]() { return sameDirPath(right->currentDir(), rightDir); }, 160),
           "fixture: right pane shows another folder");
    splitAct->trigger();
    EXPECT(!mw.isSplitBrowseEnabled(), "back to a single pane");

    // Single pane: another folder, its own sort, view mode, size, selection.
    mw.openExternalTargets({thirdDir});
    EXPECT(
        waitUntil(
            [&]()
            { return sameDirPath(left->currentDir(), thirdDir) && left->pathList().size() >= 2; },
            200),
        "single pane lists the new folder");
    left->setSort(ThumbnailPanel::SortSize, false);
    left->setViewMode(ThumbnailPanel::LargeIcon);
    const QString picked = left->pathList().value(0);
    left->selectPaths({picked}, picked);
    QApplication::processEvents();

    splitAct->trigger();
    EXPECT(mw.isSplitBrowseEnabled(), "split button enters two-column browse");
    EXPECT(sameDirPath(right->currentDir(), thirdDir),
           "right pane opens the single pane's folder, not the stale one");
    EXPECT(sameDirPath(rightPath->text(), thirdDir), "right address bar shows the same folder");
    EXPECT(waitUntil([&]() { return right->pathList().size() == left->pathList().size(); }, 200),
           "right pane lists the same contents");
    EXPECT(right->sortMode() == left->sortMode() && right->sortAscending() == left->sortAscending(),
           "right pane mirrors the sort");
    auto *rightSort = mw.findChild<QComboBox *>(QStringLiteral("secondarySortCombo"));
    EXPECT(rightSort && rightSort->currentData().toInt() == static_cast<int>(left->sortMode()),
           "right sort control shows the mirrored sort");
    EXPECT(right->viewMode() == left->viewMode() && right->thumbSize() == left->thumbSize(),
           "right pane mirrors view mode and thumbnail size");
    EXPECT(right->hiddenFormatGroups() == left->hiddenFormatGroups(),
           "right pane mirrors the type filter");
    EXPECT(left->selectedPaths() == QStringList{picked}, "left pane keeps its selection");
    EXPECT(right->selectedPaths().isEmpty(), "right pane starts without a selection");

    // After the split the panes navigate on their own.
    mw.openRightDirectory(rightDir);
    EXPECT(waitUntil([&]() { return sameDirPath(right->currentDir(), rightDir); }, 160) &&
               sameDirPath(left->currentDir(), thirdDir),
           "right pane navigates without moving the left one");
    mw.openExternalTargets({leftDir});
    EXPECT(waitUntil([&]() { return sameDirPath(left->currentDir(), leftDir); }, 160) &&
               sameDirPath(right->currentDir(), rightDir),
           "left pane navigates without moving the right one");

    // Every split mirrors again, even right after the right pane wandered off.
    splitAct->trigger();
    splitAct->trigger();
    EXPECT(sameDirPath(right->currentDir(), leftDir) && sameDirPath(rightPath->text(), leftDir),
           "re-entering split mirrors the current single pane again");
    left->setViewMode(ThumbnailPanel::Thumbnail);
    splitAct->trigger();
}
