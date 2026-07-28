// MainWindow report and image export (M20 P0#1).
#include "mainwindow_p.h"

void MainWindow::exportReport()
{
    // M14-4: collect current view data and build an HTML report.
    mviewer::core::ReportContext ctx;
    ctx.title = "MViewer Analysis Report";

    if (currentImagePath().isEmpty())
    {
        QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
        return;
    }
    ctx.imagePath = currentImagePath().toStdString();

    // Grab the histogram pixmap from the analysis panel (if rendered).
    if (m_analysisPanel)
    {
        QPixmap hist = m_analysisPanel->histogramPixmap();
        if (!hist.isNull())
        {
            QByteArray buf;
            QBuffer stream(&buf);
            hist.save(&stream, "PNG");
            ctx.histogramPng = buf.toBase64().toStdString();
        }
    }

    // Compare data (if a compare session is active).
    if (m_compareView)
    {
        const mviewer::domain::CompareSession sess = m_compareView->compareSession();
        // Only meaningful if 2+ images.
        const int n = m_compareView->engine().imageCount();
        if (n >= 2)
        {
            const ImageFrame *a = m_compareView->engine().imageAt(0);
            const ImageFrame *b = m_compareView->engine().imageAt(1);
            if (a && b)
            {
                ctx.compare = mviewer::core::buildCompareReport(*a, *b);
                ctx.hasCompare = true;
                ImageData diffImg = mviewer::core::compareDiffImage(*a, *b);
                if (!diffImg.isNull())
                {
                    // Convert to PNG base64 (via Qt).
                    QImage q = mvcore::toQImage(diffImg);
                    QByteArray buf;
                    QBuffer stream(&buf);
                    q.save(&stream, "PNG");
                    ctx.compareDiffPng = buf.toBase64().toStdString();
                }
            }
        }
    }

    const std::string html = mviewer::core::buildReportHtml(ctx);
    if (html.empty())
    {
        QMessageBox::warning(this, tr("导出报告"), tr("报告内容为空。"));
        return;
    }

    const QString out = QFileDialog::getSaveFileName(this, tr("导出报告"), QString(),
                                                     tr("HTML 文件 (*.html);;Markdown 文件 (*.md);;"
                                                        "JSON 文件 (*.json)"));
    if (out.isEmpty())
        return;
    const QFileInfo fi(out);
    const QString suffix = fi.suffix().toLower();

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("错误"), tr("无法写入：%1").arg(out));
        return;
    }
    if (suffix == "json")
    {
        // JSON: emit the compare report only (structured data).
        std::string json = "{\"error\":\"no compare data\"}";
        if (ctx.hasCompare)
            json = ctx.compare.toJson();
        f.write(QByteArray::fromStdString(json));
    }
    else if (suffix == "md")
    {
        // P1 #⑥: Markdown export — simple structured report.
        QString md;
        md += QString("# %1\n\n").arg(QString::fromStdString(ctx.title));
        md += QString("**图像**: `%1`\n\n").arg(QString::fromStdString(ctx.imagePath));
        if (!ctx.histogramPng.empty())
            md += QString("![直方图](data:image/png;base64,%1)\n\n")
                      .arg(QString::fromStdString(ctx.histogramPng));
        if (ctx.hasCompare)
        {
            md += "## 对比报告\n\n";
            md += QString::fromStdString(ctx.compare.toJson()); // structured diff as JSON block
            if (!ctx.compareDiffPng.empty())
                md += QString("\n\n![Diff](data:image/png;base64,%1)\n")
                          .arg(QString::fromStdString(ctx.compareDiffPng));
        }
        f.write(md.toUtf8());
    }
    else
    {
        f.write(QByteArray::fromStdString(html));
    }
    f.close();
    QMessageBox::information(this, tr("导出报告"), tr("已导出：%1").arg(out));
}

void MainWindow::exportImages()
{
    // A-3 / M17: SelectionModel → gallery selection → filtered (rating/flag) set
    // → full directory. Prefer the filtered visible set over the unfiltered
    // directory so "export what I see" matches the rating/flag filters.
    QStringList paths = resolveSelectedPaths(true);
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->visiblePaths(); // post-filter set
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->pathList();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("导出图片"), tr("请先打开一个图片目录。"));
        return;
    }

    ExportDialog dlg(this);
    dlg.setSources(paths);
    // Surface how many images will be exported (helps when filters are active).
    dlg.setWindowTitle(tr("导出图片 — %1 张").arg(paths.size()));
    dlg.exec();
}
