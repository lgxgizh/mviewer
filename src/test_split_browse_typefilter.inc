// Included by test_split_browse_acceptance.cpp (inside its anonymous
// namespace): browse type filter cases TC-SB-15..17.

void testFormatFilter(const QString &leftDir, const QString &rightDir)
{
    std::cout << "--- TC-SB-15: multi-select type filter, RAW hidden by default ---" << std::endl;
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    auto *leftCombo = mw.findChild<FormatFilterCombo *>(QStringLiteral("typeFilterCombo"));
    auto *rightCombo =
        mw.findChild<FormatFilterCombo *>(QStringLiteral("secondaryTypeFilterCombo"));
    EXPECT(leftCombo && rightCombo, "both panes have the type filter");
    if (!leftCombo || !rightCombo)
        return;
    EXPECT(leftCombo->isVisibleTo(&mw), "left type filter is on the visible sort row");
    EXPECT(mw.browseHiddenFormatGroups() == QStringList{QStringLiteral("raw")} &&
               leftCombo->hiddenGroups() == QStringList{QStringLiteral("raw")},
           "RAW is unchecked by default");
    const QStringList ids = mviewer::browseformats::availableGroupIds();
    EXPECT(ids.contains(QStringLiteral("bmp")), "BMP is a listed, decodable group");
    EXPECT(leftCombo->count() == ids.size() && rightCombo->count() == ids.size(),
           "combo rows match the decodable format groups");
    QStringList decodable;
    for (const std::string &suffix : mviewer::core::ImageFormats::supportedSuffixes())
        decodable.append(QString::fromStdString(suffix).toLower());
    bool extsDecodable = true;
    for (const QString &id : ids)
        for (const QString &ext : mviewer::browseformats::groupExtensions(id))
            extsDecodable = extsDecodable && decodable.contains(ext);
    EXPECT(extsDecodable, "every listed extension is decodable");

    mw.openExternalTargets({leftDir});
    mw.toggleSplitBrowse(true);
    mw.openRightDirectory(rightDir);
    ThumbnailPanel *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    ThumbnailPanel *right = mw.secondaryThumbnailPanel();
    EXPECT(waitUntil(
               [&]()
               {
                   return listsSuffix(left, QStringLiteral("bmp")) &&
                          listsSuffix(right, QStringLiteral("bmp"));
               },
               160),
           "BMP files are listed in both panes");
    EXPECT(!listsSuffix(left, QStringLiteral("dng")) && !listsSuffix(right, QStringLiteral("dng")),
           "RAW files are hidden in both panes by default");
    if (ids.contains(QStringLiteral("raw")))
    {
        leftCombo->setGroupVisible(QStringLiteral("raw"), true);
        EXPECT(rightCombo->hiddenGroups().isEmpty(), "right combo follows the left one");
        EXPECT(waitUntil(
                   [&]()
                   {
                       return listsSuffix(left, QStringLiteral("dng")) &&
                              listsSuffix(right, QStringLiteral("dng"));
                   },
                   160),
               "checking RAW shows RAW files in both panes");
        rightCombo->setGroupVisible(QStringLiteral("raw"), false);
        // Wait for the re-filtered list, not the transient empty one.
        EXPECT(waitUntil(
                   [&]()
                   {
                       return !listsSuffix(left, QStringLiteral("dng")) &&
                              listsSuffix(left, QStringLiteral("png"));
                   },
                   160),
               "unchecking RAW in the right combo hides it in the left pane too");
    }
    rightCombo->setGroupVisible(QStringLiteral("bmp"), false);
    EXPECT(waitUntil(
               [&]()
               {
                   // The list is briefly empty while it re-filters; wait
                   // for the final state (BMP gone, PNG still listed).
                   return !listsSuffix(left, QStringLiteral("bmp")) &&
                          !listsSuffix(right, QStringLiteral("bmp")) &&
                          listsSuffix(left, QStringLiteral("png")) &&
                          listsSuffix(right, QStringLiteral("png"));
               },
               240),
           "unchecking BMP hides only BMP files");
    EXPECT(mviewer::browseformats::loadHiddenGroups().contains(QStringLiteral("bmp")),
           "type filter choice is persisted");
    mw.setBrowseHiddenFormatGroups(mviewer::browseformats::defaultHiddenGroups());
}

int rowOf(const ThumbnailPanel *panel, const QString &path)
{
    return panel ? static_cast<int>(panel->pathList().indexOf(path)) : -1;
}

QString currentPathOf(const ThumbnailPanel *panel)
{
    return panel && panel->currentIndex().isValid()
               ? panel->pathList().value(panel->currentIndex().row())
               : QString();
}

int visualTopOf(ThumbnailPanel *panel, const QString &path)
{
    const int row = rowOf(panel, path);
    return row < 0 ? -100000 : panel->visualRect(panel->model()->index(row, 0)).top();
}

// Regression: toggling a type group rescanned the folder and rebuilt the
// gallery from scratch, dropping the selection and jumping to the top.
void testTypeFilterKeepsState(const QString &dir)
{
    std::cout << "--- TC-SB-16: type filter keeps selection, current and scroll ---" << std::endl;
    // BMPs sort before every PNG, so hiding them shifts all visible rows up.
    for (int i = 0; i < 24; ++i)
    {
        QImage bmp(16, 16, QImage::Format_RGB32);
        bmp.fill(Qt::blue);
        bmp.save(QDir(dir).filePath(QStringLiteral("a_%1.bmp").arg(i, 2, 10, QLatin1Char('0'))),
                 "BMP");
    }
    for (int i = 0; i < 160; ++i)
        createDummyImage(
            QDir(dir).filePath(QStringLiteral("img_%1.png").arg(i, 3, 10, QLatin1Char('0'))));
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    mw.openExternalTargets({dir});
    auto *panel = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    EXPECT(waitUntil([&]() { return panel && panel->pathList().size() == 184; }, 200),
           "type-filter fixture listed (PNG + BMP)");
    if (!panel || panel->pathList().size() != 184)
        return;
    // Use the panel's own spelling of the paths (canonical temp dir on CI).
    auto listed = [panel](const QString &name)
    {
        for (const QString &path : panel->pathList())
            if (QFileInfo(path).fileName() == name)
                return path;
        return QString();
    };
    const QString pick = listed(QStringLiteral("img_120.png"));
    const QString other = listed(QStringLiteral("img_121.png"));
    EXPECT(!pick.isEmpty() && !other.isEmpty(), "type-filter picks are listed");
    if (pick.isEmpty() || other.isEmpty())
        return;
    panel->selectPaths({pick, other}, pick);
    panel->scrollTo(panel->model()->index(rowOf(panel, pick), 0),
                    QAbstractItemView::PositionAtCenter);
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();
    const int scrollBefore = panel->verticalScrollBar()->value();
    const int topBefore = visualTopOf(panel, pick);
    const int step =
        qMax(1, panel->visualRect(panel->model()->index(rowOf(panel, pick), 0)).height());
    EXPECT(scrollBefore > 0, "fixture is scrolled away from the top");

    for (const bool hideBmp : {true, false})
    {
        mw.setBrowseHiddenFormatGroups(
            hideBmp ? QStringList{QStringLiteral("raw"), QStringLiteral("bmp")}
                    : QStringList{QStringLiteral("raw")});
        EXPECT(waitUntil(
                   [&]()
                   {
                       return panel->pathList().size() == (hideBmp ? 160 : 184) &&
                              listsSuffix(panel, QStringLiteral("bmp")) != hideBmp;
                   },
                   200),
               hideBmp ? "hiding BMP removes only the BMP rows" : "showing BMP restores them");
        for (int i = 0; i < 5; ++i)
            QApplication::processEvents();
        QStringList selected = panel->selectedPaths();
        selected.sort();
        QStringList expected{pick, other};
        expected.sort();
        EXPECT(selected == expected, "type filter change keeps the multi-selection");
        EXPECT(currentPathOf(panel) == pick, "type filter change keeps the current item");
        EXPECT(std::abs(visualTopOf(panel, pick) - topBefore) <= step / 2 &&
                   panel->verticalScrollBar()->value() > 0,
               "type filter change keeps the scroll anchor (no jump to top)");
    }
    mw.setBrowseHiddenFormatGroups(mviewer::browseformats::defaultHiddenGroups());
}

QWidget *typeHintOf(ThumbnailPanel *panel)
{
    return panel ? panel->findChild<QWidget *>(QStringLiteral("typeHiddenHint")) : nullptr;
}

// A folder of only RAW files looked empty with RAW hidden by default.
void testTypeHiddenHint(const QString &dir)
{
    std::cout << "--- TC-SB-17: hidden-by-type hint with Show in both panes ---" << std::endl;
    for (int i = 0; i < 3; ++i)
    {
        QFile raw(QDir(dir).filePath(QStringLiteral("only_%1.dng").arg(i)));
        if (raw.open(QIODevice::WriteOnly))
            raw.write("II*\0fake");
    }
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    mw.openExternalTargets({dir});
    mw.toggleSplitBrowse(true);
    mw.openRightDirectory(dir);
    ThumbnailPanel *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    ThumbnailPanel *right = mw.secondaryThumbnailPanel();
    auto hintShown = [](ThumbnailPanel *panel)
    {
        QWidget *hint = typeHintOf(panel);
        return hint && hint->isVisible();
    };
    EXPECT(waitUntil([&]() { return hintShown(left) && hintShown(right); }, 200),
           "RAW-only folder shows the hidden-by-type hint in both panes");
    EXPECT(mw.browseHiddenFormatGroups() == QStringList{QStringLiteral("raw")},
           "the hint does not change the saved type filter by itself");
    for (ThumbnailPanel *panel : {left, right})
    {
        auto *label =
            panel ? panel->findChild<QLabel *>(QStringLiteral("typeHiddenHintLabel")) : nullptr;
        EXPECT(label && label->text().contains(QLatin1Char('3')) && panel->typeHiddenCount() == 3 &&
                   panel->typeHiddenGroups() == QStringList{QStringLiteral("raw")},
               "hint names how many files the type filter hides");
    }
    auto *show =
        right ? right->findChild<QPushButton *>(QStringLiteral("typeHiddenHintShow")) : nullptr;
    EXPECT(show && show->isVisible(), "hint offers a one-click Show button");
    if (show)
        show->click();
    EXPECT(waitUntil(
               [&]()
               {
                   return left && right && left->pathList().size() == 3 &&
                          right->pathList().size() == 3;
               },
               200),
           "Show lists the hidden RAW files in both panes");
    EXPECT(!hintShown(left) && !hintShown(right), "hint disappears once the files are shown");
    EXPECT(!mw.browseHiddenFormatGroups().contains(QStringLiteral("raw")) &&
               !mviewer::browseformats::loadHiddenGroups().contains(QStringLiteral("raw")),
           "Show is an explicit choice and is persisted");
    mw.setBrowseHiddenFormatGroups(mviewer::browseformats::defaultHiddenGroups());
}
