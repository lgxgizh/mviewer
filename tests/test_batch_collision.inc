// L3: two inputs that differ only by extension convert to the same output
// name. The batch must give the later one a distinct name ("dup (2).png")
// instead of silently overwriting the first output (included by test_batch).
static void testOutputNameCollision()
{
    QTemporaryDir tmp;
    QImage first(16, 16, QImage::Format_RGB32);
    first.fill(Qt::red);
    QImage second(24, 24, QImage::Format_RGB32);
    second.fill(Qt::blue);
    const QString inPng = tmp.filePath("dup.png");
    const QString inBmp = tmp.filePath("dup.bmp");
    first.save(inPng, "PNG");
    second.save(inBmp, "BMP");
    const QString out = tmp.filePath("out");
    QDir().mkpath(out);

    mviewer::domain::BatchJobConfig config;
    config.inputPaths = {inPng.toStdString(), inBmp.toStdString()};
    config.operations = {mviewer::domain::BatchOp::Export};
    config.exportFormat = "png";
    config.outputDir = out.toStdString();
    config.overwriteExisting = true;
    const auto result = mviewer::core::BatchProcessor().execute(config);
    CHECK(result.totalSucceeded == 2, "both colliding inputs convert");
    const bool distinct = result.fileResults.size() == 2 &&
                          result.fileResults[0].outputPath != result.fileResults[1].outputPath;
    CHECK(distinct, "colliding outputs get distinct names, nothing is overwritten");
    CHECK(QFileInfo::exists(QDir(out).filePath("dup.png")) &&
              QFileInfo::exists(QDir(out).filePath("dup (2).png")),
          "second output is uniquified as 'dup (2).png'");
    CHECK(QImage(QDir(out).filePath("dup.png")).width() == 16 &&
              QImage(QDir(out).filePath("dup (2).png")).width() == 24,
          "each output holds its own source image");
}
