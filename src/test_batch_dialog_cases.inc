// Included by test_batch.cpp to test batch export guards, overwrite behaviors,
// and BatchDialog settings persistence without exceeding source line caps.

static void testBatchGuardsAndDialog(const QString &p1)
{
    // ── Test 18: a failed commit removes the .mviewer-tmp file ──
    {
        const QString srcDir = makeTempDir("mviewer_batch_badsrc_");
        const QString dstDir = makeTempDir("mviewer_batch_baddst_");
        const QString input = writePngIn(srcDir, "photo.png", qRgb(7, 8, 9));
        QDir().mkpath(dstDir + QStringLiteral("/photo.png"));
        const auto failed = exportFile(input, dstDir, "", true);
        CHECK(!failed.success && failed.errorMessage.find("Export failed: ") == 0,
              "commit onto a directory reports Export failed");
        CHECK(!hasMviewerTemp(dstDir), "failed commit leaves no .mviewer-tmp file");
        QDir(srcDir).removeRecursively();
        QDir(dstDir).removeRecursively();
    }

    // ── Test 19: huge and undecodable images get distinct messages ──
    {
        const QString dir = makeTempDir("mviewer_batch_huge_");
        const QString hugeOut = dir + QStringLiteral("/out");
        const char *wide = "89504e470d0a1a0a0000000d4948445200004e2000000008080200000093d95811";
        const char *heavy = "89504e470d0a1a0a0000000d4948445200002328000023280802000000e2b7e5ed";
        const auto tooWide =
            exportFile(writeBytes(dir + QStringLiteral("/huge.png"), pngClaim(wide)), hugeOut,
                       "x_{name}", false);
        CHECK(tooWide.errorMessage == "Image too large (20000x8); batch processing is not supported",
              "an image wider than 16384 reports Image too large");
        const auto tooHeavy =
            exportFile(writeBytes(dir + QStringLiteral("/heavy.png"), pngClaim(heavy)), hugeOut,
                       "y_{name}", false);
        CHECK(tooHeavy.errorMessage == "Image too large (9000x9000); batch processing is not supported",
              "an image over 256MB reports Image too large");
        const auto undecoded =
            exportFile(writeBytes(dir + QStringLiteral("/junk.png"), QByteArray("not a png")),
                       hugeOut, "z_{name}", false);
        CHECK(undecoded.errorMessage == "Unable to decode image", "undecodable image reports Unable to decode image");
        QDir(dir).removeRecursively();
    }

    // ── Test 21: directory scan uses ImageFormats, not a private list ──
    {
        const QString dir = makeTempDir("mviewer_batch_scan_");
        const QString scanOut = makeTempDir("mviewer_batch_scanout_");
        writePngIn(dir, "ok.png", qRgb(3, 3, 3));
        writeBytes(dir + QStringLiteral("/notes.txt"), QByteArray("note"));
        writeBytes(dir + QStringLiteral("/also.cr3"), QByteArray("raw"));
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {dir.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = scanOut.toStdString();
        config.renamePattern = "scan_{name}";
        const auto result = mviewer::core::BatchProcessor().execute(config);
        bool sawCr3 = false;
        bool sawTxt = false;
        for (const auto &file : result.fileResults)
        {
            sawCr3 = sawCr3 || file.inputPath.ends_with(".cr3");
            sawTxt = sawTxt || file.inputPath.ends_with(".txt");
        }
        CHECK(sawCr3, "directory scan includes cr3 from ImageFormats");
        CHECK(!sawTxt, "directory scan skips a txt file");
        QDir(dir).removeRecursively();
        QDir(scanOut).removeRecursively();
    }

    // ── Test 22: overwriteExisting defaults off; result lines stay explicit ──
    {
        mviewer::domain::BatchJobConfig config;
        CHECK(!config.overwriteExisting, "overwriteExisting defaults to false");
        mviewer::domain::BatchFileResult analyzed;
        analyzed.success = true;
        analyzed.inputPath = "C:/in.png";
        CHECK(BatchDialog::formatResultLine(analyzed) ==
                  QStringLiteral("[OK] C:/in.png (analysis only, file not written)"),
              "analysis-only success does not show an empty arrow");
        mviewer::domain::BatchFileResult exported;
        exported.success = true;
        exported.inputPath = "C:/in.png";
        exported.outputPath = "C:/out/in.png";
        CHECK(BatchDialog::formatResultLine(exported) ==
                  QStringLiteral("[OK] C:/in.png → C:/out/in.png"),
              "export success still shows input and output");
    }

    // ── Test 23: overwrite checkbox and the no-export guard ──
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::tempPath() + "/mviewer_batch_settings");
        QCoreApplication::setOrganizationName(QStringLiteral("mviewer-test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_batch"));
        QSettings settings;
        settings.remove(QStringLiteral("batch/overwriteExisting"));
        BatchDialog fresh;
        auto *box = fresh.findChild<QCheckBox *>(QStringLiteral("batchOverwriteExisting"));
        CHECK(box != nullptr && !box->isChecked(), "overwrite checkbox starts unchecked");
        settings.setValue(QStringLiteral("batch/overwriteExisting"), true);
        BatchDialog loaded;
        auto *loadedBox = loaded.findChild<QCheckBox *>(QStringLiteral("batchOverwriteExisting"));
        CHECK(loadedBox != nullptr && loadedBox->isChecked(),
              "overwrite checkbox restores batch/overwriteExisting");
        auto *exportBox = loaded.findChild<QCheckBox *>(QStringLiteral("batchChkExport"));
        auto *resizeBox = loaded.findChild<QCheckBox *>(QStringLiteral("batchChkResize"));
        auto *start = loaded.findChild<QPushButton *>(QStringLiteral("batchStartButton"));
        auto *log = loaded.findChild<QTextEdit *>(QStringLiteral("batchLog"));
        loaded.setInputFiles({p1});
        CHECK(exportBox && resizeBox && start && log, "batch export guard controls exist");
        if (exportBox && resizeBox && start)
        {
            exportBox->setChecked(false);
            resizeBox->setChecked(true);
            QString seen;
            QTimer::singleShot(0,
                               [&seen]()
                               {
                                   auto *message = qobject_cast<QMessageBox *>(
                                       QApplication::activeModalWidget());
                                   if (!message)
                                       return;
                                   seen = message->text();
                                   message->reject();
                               });
            start->click();
            CHECK(start->isEnabled(), "resize without Export does not start the batch");
            CHECK(seen.contains(QStringLiteral("Please check Export")),
                  "resize without Export warns to check Export");
            CHECK(log && !log->toPlainText().contains(QStringLiteral("[OK]")),
                  "resize without Export writes no result line");
        }
        settings.remove(QStringLiteral("batch/overwriteExisting"));
    }
}
