// Included by test_split_browse_acceptance.cpp (inside its anonymous
// namespace): browse type filter cases TC-SB-15..18.

void testFormatFilter(const QString &leftDir, const QString &rightDir)
{
    std::cout << "--- TC-SB-15: multi-select type filter, RAW and Gray hidden by default ---"
              << std::endl;
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
    const QStringList hiddenDefault{QStringLiteral("raw"), QStringLiteral("gray")};
    EXPECT(mw.browseHiddenFormatGroups() == hiddenDefault &&
               leftCombo->hiddenGroups() == hiddenDefault,
           "RAW and Gray are unchecked by default");
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
        const QStringList afterShowRaw{QStringLiteral("gray")};
        EXPECT(rightCombo->hiddenGroups() == afterShowRaw,
               "right combo follows the left one (Gray stays hidden)");
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
    EXPECT(mw.browseHiddenFormatGroups() ==
               (QStringList{QStringLiteral("raw"), QStringLiteral("gray")}),
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

void testFormatFilterEveryGroup(const QString &dir)
{
    std::cout << "--- TC-SB-18: every type group hides exactly its files "
                 "(sensor RAW visible; gray hidden by default) ---"
              << std::endl;

    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("gray")) == QStringLiteral("gray"),
           "groupForSuffix(\"gray\") is gray");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("GREY")) == QStringLiteral("gray"),
           "groupForSuffix(\"GREY\") is gray");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral(".gray")) ==
               QStringLiteral("gray"),
           "groupForSuffix(\".gray\") is gray");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("raw")) ==
               QStringLiteral("sensorraw"),
           "groupForSuffix(\"raw\") is sensorraw");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("RAW")) ==
               QStringLiteral("sensorraw"),
           "groupForSuffix(\"RAW\") is sensorraw");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral(".raw")) ==
               QStringLiteral("sensorraw"),
           "groupForSuffix(\".raw\") is sensorraw");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("dng")) == QStringLiteral("raw"),
           "groupForSuffix(\"dng\") is raw");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("jpg")) == QStringLiteral("jpeg"),
           "groupForSuffix(\"jpg\") is jpeg");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("tif")) == QStringLiteral("tiff"),
           "groupForSuffix(\"tif\") is tiff");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("heif")) == QStringLiteral("heic"),
           "groupForSuffix(\"heif\") is heic");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("dib")) == QStringLiteral("bmp"),
           "groupForSuffix(\"dib\") is bmp");
    EXPECT(mviewer::browseformats::groupForSuffix(QStringLiteral("yuv")) == QStringLiteral("other"),
           "groupForSuffix(\"yuv\") is other");
    const QStringList grayExts = mviewer::browseformats::groupExtensions(QStringLiteral("gray"));
    EXPECT(grayExts.contains(QStringLiteral("gray")) && grayExts.contains(QStringLiteral("grey")),
           "groupExtensions(\"gray\") contains gray and grey");
    const QStringList sensorExts =
        mviewer::browseformats::groupExtensions(QStringLiteral("sensorraw"));
    EXPECT(sensorExts.contains(QStringLiteral("raw")),
           "groupExtensions(\"sensorraw\") contains \"raw\"");
    EXPECT(!sensorExts.contains(QStringLiteral("gray")) &&
               !sensorExts.contains(QStringLiteral("grey")),
           "groupExtensions(\"sensorraw\") contains neither gray nor grey");
    EXPECT(!mviewer::browseformats::groupExtensions(QStringLiteral("raw"))
                .contains(QStringLiteral("raw")),
           "groupExtensions(\"raw\") does not contain \"raw\"");
    EXPECT(!mviewer::browseformats::groupExtensions(QStringLiteral("other"))
                .contains(QStringLiteral("raw")),
           "groupExtensions(\"other\") does not contain \"raw\"");
    EXPECT(mviewer::browseformats::isPathHidden(QStringLiteral("/x/a.raw"),
                                                QStringList{QStringLiteral("sensorraw")}),
           "isPathHidden(\"/x/a.raw\", {\"sensorraw\"}) is true");
    EXPECT(!mviewer::browseformats::isPathHidden(QStringLiteral("/x/a.raw"),
                                                 QStringList{QStringLiteral("raw")}),
           "isPathHidden(\"/x/a.raw\", {\"raw\"}) is false");
    EXPECT(!mviewer::browseformats::isPathHidden(QStringLiteral("/x/a.raw"),
                                                 QStringList{QStringLiteral("other")}),
           "isPathHidden(\"/x/a.raw\", {\"other\"}) is false");
    EXPECT(mviewer::browseformats::isPathHidden(QStringLiteral("/x/a.grey"),
                                                QStringList{QStringLiteral("gray")}),
           "isPathHidden(\"/x/a.grey\", {\"gray\"}) is true");
    EXPECT(!mviewer::browseformats::isPathHidden(QStringLiteral("/x/a.grey"),
                                                 QStringList{QStringLiteral("sensorraw")}),
           "isPathHidden(\"/x/a.grey\", {\"sensorraw\"}) is false");
    EXPECT(mviewer::browseformats::availableGroupIds().contains(QStringLiteral("sensorraw")),
           "availableGroupIds() contains \"sensorraw\"");
    EXPECT(mviewer::browseformats::availableGroupIds().contains(QStringLiteral("gray")),
           "availableGroupIds() contains \"gray\"");
    const QStringList hiddenDefault{QStringLiteral("raw"), QStringLiteral("gray")};
    EXPECT(!mviewer::browseformats::defaultHiddenGroups().contains(QStringLiteral("sensorraw")),
           "defaultHiddenGroups() does not contain \"sensorraw\"");
    EXPECT(mviewer::browseformats::defaultHiddenGroups() == hiddenDefault,
           "defaultHiddenGroups() is {raw, gray}");

    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroupsVersion"));
    EXPECT(mviewer::browseformats::loadHiddenGroups() == hiddenDefault,
           "default hidden set is {raw, gray}");
    EXPECT(!QSettings().contains(QStringLiteral("browse/hiddenFormatGroups")) &&
               !QSettings().contains(QStringLiteral("browse/hiddenFormatGroupsVersion")),
           "a missing hidden-groups key is not written");
    {
        QSettings settings;
        settings.setValue(QStringLiteral("browse/hiddenFormatGroups"),
                          QStringList{QStringLiteral("raw"), QStringLiteral("bmp")});
        settings.remove(QStringLiteral("browse/hiddenFormatGroupsVersion"));
    }
    const QStringList migrated = mviewer::browseformats::loadHiddenGroups();
    EXPECT(migrated.contains(QStringLiteral("raw")) && migrated.contains(QStringLiteral("bmp")) &&
               migrated.contains(QStringLiteral("gray")) &&
               !migrated.contains(QStringLiteral("sensorraw")),
           "saved {raw, bmp} with no version gains gray and not sensorraw");
    EXPECT(QSettings().value(QStringLiteral("browse/hiddenFormatGroupsVersion")).toInt() == 2,
           "migration stores hidden format groups version 2");
    mviewer::browseformats::saveHiddenGroups({QStringLiteral("raw")});
    EXPECT(mviewer::browseformats::loadHiddenGroups() == QStringList{QStringLiteral("raw")},
           "saving {raw} leaves gray visible");
    {
        QSettings settings;
        settings.setValue(QStringLiteral("browse/hiddenFormatGroups"),
                          QStringList{QStringLiteral("none")});
        settings.remove(QStringLiteral("browse/hiddenFormatGroupsVersion"));
    }
    EXPECT(mviewer::browseformats::loadHiddenGroups() == QStringList{QStringLiteral("gray")},
           "stored {none} without a version migrates to {gray}");
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroupsVersion"));

    bool everyDecodableInListedGroup = true;
    const QStringList listedIds = mviewer::browseformats::availableGroupIds();
    for (const std::string &ext : mviewer::core::ImageFormats::supportedSuffixes())
    {
        const QString group = mviewer::browseformats::groupForSuffix(QString::fromStdString(ext));
        if (!listedIds.contains(group))
            everyDecodableInListedGroup = false;
    }
    EXPECT(everyDecodableInListedGroup, "every decodable suffix lands in a listed group");

    const QStringList names{
        QStringLiteral("a.jpg"), QStringLiteral("b.png"),       QStringLiteral("c.webp"),
        QStringLiteral("d.tif"), QStringLiteral("e.heic"),      QStringLiteral("f.gif"),
        QStringLiteral("g.bmp"), QStringLiteral("h.dng"),       QStringLiteral("i.raw"),
        QStringLiteral("j.yuv"), QStringLiteral("g_16x16.gray")};
    QStringList kept;
    for (const QString &name : names)
    {
        const QString suffix = QFileInfo(name).suffix().toLower();
        if (!mviewer::core::ImageFormats::isSupportedSuffix(suffix.toStdString()))
            continue;
        const QString path = QDir(dir).filePath(name);
        if (suffix == QLatin1String("png") || suffix == QLatin1String("bmp"))
        {
            QImage img(16, 16, QImage::Format_RGB32);
            img.fill(Qt::blue);
            img.save(path, suffix == QLatin1String("png") ? "PNG" : "BMP");
        }
        else if (suffix == QLatin1String("gray"))
        {
            // 16x16 8-bit headerless gray (ADR 034): 256 payload bytes.
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(QByteArray(256, '\0'));
        }
        else
        {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write("dummy");
        }
        kept.append(suffix);
    }

    EXPECT(kept.contains(QStringLiteral("gray")), ".gray fixture is a decodable suffix");
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroups"));
    QSettings().remove(QStringLiteral("browse/hiddenFormatGroupsVersion"));
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    mw.openExternalTargets({dir});
    ThumbnailPanel *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    if (kept.contains(QStringLiteral("raw")) && kept.contains(QStringLiteral("dng")) &&
        kept.contains(QStringLiteral("gray")))
    {
        EXPECT(waitUntil(
                   [&]()
                   {
                       if (!left)
                           return false;
                       return listsSuffix(left, QStringLiteral("raw")) &&
                              !listsSuffix(left, QStringLiteral("dng")) &&
                              !listsSuffix(left, QStringLiteral("gray"));
                   },
                   8000),
               "default: sensor RAW shown; camera RAW and gray hidden");
    }
    mw.setBrowseHiddenFormatGroups(QStringList{});
    EXPECT(waitUntil(
               [&]()
               {
                   if (!left)
                       return false;
                   for (const QString &suffix : kept)
                       if (!listsSuffix(left, suffix))
                           return false;
                   return true;
               },
               8000),
           "all kept fixtures are listed");

    for (const QString &id : mviewer::browseformats::availableGroupIds())
    {
        mw.setBrowseHiddenFormatGroups(QStringList{id});
        const std::string msg =
            std::string("hiding '") + id.toStdString() + "' hides exactly that group's files";
        EXPECT(waitUntil(
                   [&]()
                   {
                       if (!left)
                           return false;
                       for (const QString &suffix : kept)
                       {
                           const bool hidden = mviewer::browseformats::groupForSuffix(suffix) == id;
                           if (listsSuffix(left, suffix) == hidden)
                               return false;
                       }
                       return true;
                   },
                   5000),
               msg);
        if (id == QLatin1String("raw"))
        {
            EXPECT(!listsSuffix(left, QStringLiteral("dng")) &&
                       listsSuffix(left, QStringLiteral("raw")),
                   "hiding raw removes dng; raw stays");
        }
        if (id == QLatin1String("sensorraw"))
        {
            EXPECT(!listsSuffix(left, QStringLiteral("raw")) &&
                       listsSuffix(left, QStringLiteral("dng")) &&
                       listsSuffix(left, QStringLiteral("gray")),
                   "hiding sensorraw removes raw; dng and gray stay");
        }
        if (id == QLatin1String("gray"))
        {
            EXPECT(!listsSuffix(left, QStringLiteral("gray")) &&
                       listsSuffix(left, QStringLiteral("raw")),
                   "hiding gray removes gray; raw stays");
        }
        if (id == QLatin1String("other"))
        {
            EXPECT(listsSuffix(left, QStringLiteral("raw")), "hiding other still lists raw");
        }
    }
    mw.setBrowseHiddenFormatGroups(mviewer::browseformats::defaultHiddenGroups());
}
