// Included by test_batch_dialog_cases.inc: core::renameInPlace and the
// rename-only BatchDialog (folder auto-load, live preview, in-place rename).

static QString fileText(const QString &path)
{
    return QString::fromUtf8(readAllBytes(path));
}

static std::string u8(const QString &path)
{
    return path.toUtf8().toStdString();
}

static void testRenameInPlaceCore()
{
    using mviewer::core::InPlaceRenameOutcome;
    using mviewer::core::renameInPlace;
    const QString dir = makeTempDir("mviewer_rename_core_");
    const QString a = writeBytes(dir + QStringLiteral("/a.jpg"), QByteArray("A"));
    const QString b = writeBytes(dir + QStringLiteral("/b.jpg"), QByteArray("B"));
    const QString c = writeBytes(dir + QStringLiteral("/c.jpg"), QByteArray("C"));
    const QString keep = writeBytes(dir + QStringLiteral("/keep.jpg"), QByteArray("K"));
    std::vector<std::string> trashed;
    const auto fakeTrash = [&trashed](const std::string &path)
    {
        trashed.push_back(path);
        return QFile::remove(QString::fromStdString(path));
    };

    // Swap a <-> b goes through temporary names; c is renamed plainly.
    auto report = renameInPlace({{u8(a), "b.jpg"}, {u8(b), "a.jpg"}, {u8(c), "c2.jpg"}}, fakeTrash);
    CHECK(report.renamed == 3 && report.failed == 0 && report.replaced == 0,
          "swap and plain rename all succeed");
    CHECK(fileText(dir + "/a.jpg") == "B" && fileText(dir + "/b.jpg") == "A",
          "a <-> b swap keeps both files");
    CHECK(fileText(dir + "/c2.jpg") == "C" && !QFileInfo::exists(c), "c.jpg became c2.jpg");
    CHECK(trashed.empty(), "a swap trashes nothing");

    // Unrelated existing file: moved to the trash, then replaced.
    report = renameInPlace({{u8(dir + "/c2.jpg"), "keep.jpg"}}, fakeTrash);
    CHECK(report.renamed == 1 && report.replaced == 1 && report.items[0].replacedExisting,
          "an unrelated existing target is replaced");
    CHECK(trashed.size() == 1 && QString::fromStdString(trashed[0]).endsWith("keep.jpg"),
          "the replaced file goes through the trash function");
    CHECK(fileText(keep) == "C", "the renamed file now holds the target name");

    // Trash refused: the entry fails and both files stay as they were.
    const QString d = writeBytes(dir + QStringLiteral("/d.jpg"), QByteArray("D"));
    report = renameInPlace({{u8(d), "keep.jpg"}}, [](const std::string &) { return false; });
    CHECK(report.failed == 1 && report.renamed == 0, "a refused trash fails the entry");
    CHECK(fileText(d) == "D" && fileText(keep) == "C", "a refused trash keeps both files");

    // Unchanged, duplicate and invalid targets.
    report = renameInPlace({{u8(d), "d.jpg"}}, fakeTrash);
    CHECK(report.unchanged == 1 && report.items[0].outcome == InPlaceRenameOutcome::Unchanged,
          "same name is Unchanged");
    report = renameInPlace({{u8(d), "bad/name.jpg"}}, fakeTrash);
    CHECK(report.failed == 1 && QFileInfo::exists(d), "an invalid name fails");

    // Case-only rename (one file on Windows) goes through the temp name.
    report = renameInPlace({{u8(d), "D.jpg"}}, fakeTrash);
    const QStringList names = QDir(dir).entryList(QDir::Files);
    CHECK(report.renamed == 1 && names.contains(QStringLiteral("D.jpg")) &&
              !names.contains(QStringLiteral("d.jpg")),
          "a case-only rename succeeds");
    const QString upper = dir + QStringLiteral("/D.jpg");

    // Duplicate targets: the second one fails and keeps its source.
    report = renameInPlace({{u8(upper), "dup.jpg"}, {u8(keep), "dup.jpg"}}, fakeTrash);
    CHECK(report.renamed == 1 && report.failed == 1 && fileText(keep) == "C" &&
              fileText(dir + "/dup.jpg") == "D",
          "a duplicate target fails without touching its source");
    const QStringList leftovers =
        QDir(dir).entryList({QStringLiteral("*.tmp")}, QDir::Files | QDir::Hidden);
    CHECK(leftovers.isEmpty(), "in-place rename leaves no temporary files");
    QDir(dir).removeRecursively();
}

static void testBatchRenameDialog()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir::tempPath() + "/mviewer_batch_settings");
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-test"));
    QCoreApplication::setApplicationName(QStringLiteral("test_batch"));
    QSettings().remove(QStringLiteral("batchRename"));

    const QString dir = makeTempDir("mviewer_rename_dialog_");
    const QStringList images = {writePngIn(dir, "p1.png", qRgb(1, 2, 3)),
                                writePngIn(dir, "p2.png", qRgb(4, 5, 6)),
                                writePngIn(dir, "p3.png", qRgb(7, 8, 9))};
    writePngIn(dir, "x_02.png", qRgb(9, 9, 9)); // not in the batch: gets replaced

    BatchDialog dialog;
    int confirmed = -1;
    std::vector<std::string> trashed;
    dialog.setReplaceConfirmation(
        [&confirmed](int count)
        {
            confirmed = count;
            return true;
        });
    dialog.setTrashFunction(
        [&trashed](const std::string &path)
        {
            trashed.push_back(path);
            return QFile::remove(QString::fromStdString(path));
        });
    dialog.setFolder(dir, images);
    CHECK(dialog.filePaths() == images, "setFolder loads every image in gallery order");
    auto *pattern = dialog.findChild<QLineEdit *>(QStringLiteral("batchRenamePattern"));
    auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("batchRenameList"));
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("batchSummaryLabel"));
    auto *renameBtn = dialog.findChild<QPushButton *>(QStringLiteral("batchRenameButton"));
    CHECK(pattern && list && summary && renameBtn, "rename dialog controls exist");
    if (!pattern || !list || !summary || !renameBtn)
        return;

    pattern->setText(QStringLiteral("x_{seq:2}"));
    CHECK(list->topLevelItem(1)->text(1) == QStringLiteral("x_02.png"), "preview shows x_02.png");
    CHECK(summary->text().contains(QStringLiteral("1 existing file")),
          "summary warns that one existing file will be replaced");

    // Unchecking p2 renumbers the rest and shows a dash for the skipped row.
    list->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
    CHECK(list->topLevelItem(1)->text(1) == QStringLiteral("—") &&
              list->topLevelItem(2)->text(1) == QStringLiteral("x_02.png"),
          "unchecked rows are left out of the numbering");
    CHECK(dialog.checkedPaths().size() == 2, "two rows stay checked");

    QString seenSummary;
    QObject::connect(&dialog, &BatchDialog::filesRenamed, &dialog,
                     [&seenSummary](const QString &, const QString &text) { seenSummary = text; });
    CHECK(dialog.renameNow(), "renameNow runs the batch");
    CHECK(confirmed == 1, "replacing an unrelated file asks once");
    CHECK(trashed.size() == 1, "the replaced file goes to the trash");
    CHECK(QFileInfo::exists(dir + "/x_01.png") && QFileInfo::exists(dir + "/x_02.png") &&
              QFileInfo::exists(images[1]) && !QFileInfo::exists(images[0]) &&
              !QFileInfo::exists(images[2]),
          "checked files are renamed in place; the unchecked one stays");
    CHECK(seenSummary.contains(QStringLiteral("Renamed 2 files")),
          "filesRenamed carries a summary");
    CHECK(QSettings().value(QStringLiteral("batchRename/pattern")).toString() ==
              QStringLiteral("x_{seq:2}"),
          "a confirmed run remembers the pattern");

    // Declining the replacement confirmation renames nothing.
    dialog.setFolder(dir, {images[1]});
    dialog.setReplaceConfirmation([](int) { return false; });
    pattern->setText(QStringLiteral("x_01"));
    CHECK(!dialog.renameNow() && QFileInfo::exists(images[1]),
          "declining the replacement keeps every file");
    QSettings().remove(QStringLiteral("batchRename"));
    QDir(dir).removeRecursively();
}
