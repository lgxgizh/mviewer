// Included by test_split_browse_acceptance.cpp (inside its anonymous
// namespace): split-browse selection, folder tree, active-pane commands,
// the right address bar, and left-only filters.

bool titleShowsFolder(QLabel *title, const QString &name)
{
    if (!title || name.isEmpty())
        return false;
    if (title->text() == name)
        return true;
    return title->toolTip().section(QLatin1Char('\n'), 0, 0) == name;
}

void pumpUi(int turns)
{
    for (int i = 0; i < turns; ++i)
    {
        QApplication::processEvents();
        QThread::msleep(25);
    }
}

// Regression: left navigation, the folder tree, active-pane commands, the
// right address bar, and filters stay on the pane they belong to.
void testSplitBrowseState(const QString &leftDir, const QString &rightDir, const QString &thirdDir)
{
    std::cout << "--- TC-SB-19: split browse pane state ---" << std::endl;
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();

    mw.openExternalTargets({leftDir});
    ThumbnailPanel *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    EXPECT(waitUntil(
               [&]()
               {
                   return left && sameDirPath(left->currentDir(), leftDir) &&
                          left->pathList().size() >= 2;
               },
               200),
           "left pane listed its folder");
    if (!left)
        return;

    auto *search = mw.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    EXPECT(search != nullptr, "search field exists");
    if (search)
        search->setText(QStringLiteral("_a"));
    EXPECT(waitUntil([&]() { return left->pathList().size() == 1; }, 200),
           "filename search filters the single pane");

    mw.toggleSplitBrowse(true);
    ThumbnailPanel *right = mw.secondaryThumbnailPanel();
    EXPECT(right != nullptr, "right pane exists");
    if (!right)
        return;
    EXPECT(waitUntil([&]() { return sameDirPath(right->currentDir(), leftDir); }, 200),
           "opening split mirrors the left folder");
    if (search)
        search->clear();
    mw.openRightDirectory(rightDir);
    EXPECT(waitUntil(
               [&]()
               {
                   return sameDirPath(right->currentDir(), rightDir) &&
                          right->pathList().size() >= 2 && left->pathList().size() >= 2;
               },
               200),
           "right pane lists its own folder after the search is cleared");

    auto *pathEdit = mw.findChild<QLineEdit *>(QStringLiteral("secondaryPathEdit"));
    EXPECT(pathEdit != nullptr, "right address bar exists");
    if (!pathEdit || right->pathList().isEmpty())
        return;

    const QString rightBefore = right->currentDir();
    const QString rightFile = right->pathList().value(0);
    pathEdit->setText(QStringLiteral("Z:/no/such/folder"));
    QMetaObject::invokeMethod(pathEdit, "returnPressed");
    QApplication::processEvents();
    EXPECT(sameDirPath(right->currentDir(), rightBefore), "a bad path does not change the folder");
    EXPECT(QDir::cleanPath(QDir::fromNativeSeparators(pathEdit->text())) ==
               QDir::cleanPath(rightBefore),
           "a bad path restores the address bar");
    EXPECT(mw.statusBar() &&
               mw.statusBar()->currentMessage().contains(QStringLiteral("Path does not exist")),
           "a bad path reports a status message");

    pathEdit->setText(rightFile);
    QMetaObject::invokeMethod(pathEdit, "returnPressed");
    QApplication::processEvents();
    EXPECT(sameDirPath(right->currentDir(), rightBefore),
           "a file path is not opened as a directory");

    pathEdit->setText(leftDir);
    QMetaObject::invokeMethod(pathEdit, "returnPressed");
    EXPECT(waitUntil([&]() { return sameDirPath(right->currentDir(), leftDir); }, 160),
           "a real directory from the right address bar opens");
    mw.openRightDirectory(rightDir);
    EXPECT(
        waitUntil(
            [&]()
            { return sameDirPath(right->currentDir(), rightDir) && right->pathList().size() >= 2; },
            200),
        "right pane restored to its folder");

    // Empty models publish selection onto the active pane. Stay on the left
    // so a filter does not wipe the right pane's slot.
    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    auto *rating = mw.findChild<QComboBox *>(QStringLiteral("ratingFilterCombo"));
    EXPECT(rating != nullptr, "rating filter exists");
    if (rating)
    {
        rating->setCurrentIndex(5);
        EXPECT(waitUntil([&]() { return left->pathList().isEmpty(); }, 200),
               "rating filter applies to the left gallery");
        rating->setCurrentIndex(0);
        EXPECT(waitUntil([&]()
                         { return left->pathList().size() >= 2 && right->pathList().size() >= 2; },
                         200),
               "clearing the rating filter restores both panes");
    }

    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
    const QString rightPick = right->pathList().value(0);
    right->selectPath(rightPick);
    QApplication::processEvents();
    SelectionModel *sel = mw.selectionModel();
    EXPECT(sel != nullptr, "selection model exists");
    if (!sel)
        return;
    const QStringList secondaryBefore = sel->paneSelection(SelectionModel::PaneSlot::Secondary);
    mw.openExternalTargets({thirdDir});
    EXPECT(sel->paneSelection(SelectionModel::PaneSlot::Secondary) == secondaryBefore,
           "changing the left folder keeps the right selection");
    EXPECT(waitUntil(
               [&]()
               {
                   const QString primary = sel->paneCurrentImage(SelectionModel::PaneSlot::Primary);
                   return sameDirPath(left->currentDir(), thirdDir) && !primary.isEmpty() &&
                          left->pathList().contains(primary) &&
                          DirectoryTree::equivalentPath(QFileInfo(primary).absolutePath(),
                                                        left->currentDir());
               },
               200),
           "auto-select after a left load targets the left pane");
    EXPECT(sel->paneSelection(SelectionModel::PaneSlot::Secondary) == secondaryBefore,
           "the right selection stays after the left scan");
    EXPECT(mw.activeBrowsePane() == SelectionModel::PaneSlot::Secondary,
           "the active pane stays on the right");

    auto *tree = mw.findChild<DirectoryTree *>();
    EXPECT(waitUntil(
               [&]()
               {
                   return tree &&
                          DirectoryTree::equivalentPath(tree->currentPath(), left->currentDir());
               },
               80),
           "folder tree matches the left gallery");
    QString otherRight = right->pathList().value(0);
    if (sel->currentImage() == otherRight)
        otherRight = right->pathList().value(1);
    if (!otherRight.isEmpty())
        right->selectPath(otherRight);
    pumpUi(40);
    EXPECT(tree && DirectoryTree::equivalentPath(tree->currentPath(), left->currentDir()) &&
               !DirectoryTree::equivalentPath(tree->currentPath(), right->currentDir()),
           "a right-pane image does not retarget the folder tree");

    auto *title = mw.findChild<QLabel *>(QStringLiteral("folderTitleLabel"));
    const QString rightName = QFileInfo(right->currentDir()).fileName();
    const QString leftName = QFileInfo(left->currentDir()).fileName();
    EXPECT(titleShowsFolder(title, rightName), "toolbar title follows the active right pane");
    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    EXPECT(titleShowsFolder(title, leftName), "toolbar title follows the active left pane");
    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
    EXPECT(titleShowsFolder(title, rightName), "toolbar title returns to the right pane");

    auto *fav = mw.findChild<QAction *>(QStringLiteral("addFavoriteAction"));
    EXPECT(fav && fav->isEnabled(), "favorite can add the active folder");
    if (fav)
        fav->trigger();
    EXPECT(mw.statusBar() && mw.statusBar()->currentMessage().contains(rightName) &&
               !mw.statusBar()->currentMessage().contains(leftName),
           "favorite adds the active right folder");

    auto *focusPath = mw.findChild<QAction *>(QStringLiteral("focusPathAction"));
    auto *leftPath = mw.findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    EXPECT(focusPath && leftPath, "address bars and the Alt+D action exist");
    auto fullySelected = [](const QLineEdit *edit)
    { return edit && !edit->text().isEmpty() && edit->selectedText() == edit->text(); };
    if (pathEdit)
        pathEdit->deselect();
    if (leftPath)
        leftPath->deselect();
    if (focusPath)
        focusPath->trigger();
    EXPECT(fullySelected(pathEdit) && leftPath && !leftPath->hasSelectedText(),
           "Alt+D selects the right address bar");
    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    if (pathEdit)
        pathEdit->deselect();
    if (leftPath)
        leftPath->deselect();
    if (focusPath)
        focusPath->trigger();
    EXPECT(fullySelected(leftPath) && pathEdit && !pathEdit->hasSelectedText(),
           "Alt+D selects the left address bar");

    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
    pathEdit->setFocus(Qt::OtherFocusReason);
    pathEdit->setText(QDir::toNativeSeparators(right->currentDir()));
    pathEdit->setCursorPosition(1);
    pathEdit->deselect();
    QKeyEvent ctrlF(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
    QApplication::sendEvent(pathEdit, &ctrlF);
    QApplication::processEvents();
    EXPECT(!pathEdit->hasSelectedText() && QApplication::focusWidget() != leftPath,
           "Ctrl+F does not steal a focused right-pane line edit");

    if (search)
        search->setText(QStringLiteral("_a"));
    EXPECT(waitUntil([&]() { return left->pathList().size() == 1; }, 200),
           "filename search filters the left gallery while split is open");
    auto *refresh = mw.findChild<QAction *>(QStringLiteral("refreshDirectoryAction"));
    EXPECT(refresh && refresh->isEnabled(), "refresh is enabled for the active right folder");
    if (refresh)
        refresh->trigger();
    EXPECT(waitUntil([&]() { return right->pathList().size() >= 2; }, 200),
           "refresh of the right pane relists its folder");
    pumpUi(40);
    EXPECT(sameDirPath(right->currentDir(), rightDir), "refresh keeps the right folder");
    if (search)
        search->clear();
}
