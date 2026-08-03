// MainWindow report and image export (M20 P0#1).
#include "mainwindow_p.h"

void MainWindow::exportReport()
{
    mviewer::core::ReportContext ctx;
    ctx.title = "MViewer Analysis Report";

    if (m_compareView && m_compareView->comparedImageCount() >= 2)
    {
        // Capture the compare state once. Every output format reads this snapshot.
        ctx.compareBundle = m_compareView->buildReportBundle();
        ctx.hasCompareBundle = true;
    }

    if (!ctx.hasCompareBundle)
    {
        if (currentImagePath().isEmpty())
        {
            QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
            return;
        }

        ctx.imagePath = currentImagePath().toStdString();
    }
    else
    {
        const bool validReference = ctx.compareBundle.referenceIndex >= 0 &&
                                    ctx.compareBundle.referenceIndex <
                                        static_cast<int>(ctx.compareBundle.images.size());
        if (validReference)
        {
            ctx.imagePath =
                ctx.compareBundle.images[static_cast<size_t>(ctx.compareBundle.referenceIndex)];
        }
        else if (m_compareView)
        {
            ctx.imagePath = m_compareView->focusImagePath().toStdString();
        }
    }
    if (ctx.imagePath.empty())
    {
        QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
        return;
    }

    if (!ctx.hasCompareBundle && m_analysisPanel)
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

    if (ctx.hasCompareBundle)
    {
        ctx.compareDiffPngs.resize(ctx.compareBundle.targets.size());
        for (size_t i = 0; i < ctx.compareBundle.targets.size(); ++i)
        {
            const ImageData &diffImg = ctx.compareBundle.targets[i].diffHeatmap;
            if (diffImg.isNull())
                continue;
            QImage q = mvcore::toQImage(diffImg);
            QByteArray buf;
            QBuffer stream(&buf);
            q.save(&stream, "PNG");
            ctx.compareDiffPngs[i] = buf.toBase64().toStdString();
        }
    }

    const std::string html = mviewer::core::buildReportHtml(ctx);
    if (html.empty())
    {
        QMessageBox::warning(this, tr("导出报告"), tr("报告内容为空。"));
        return;
    }

    const QString out = QFileDialog::getSaveFileName(
        this, tr("导出报告"), QString(),
        tr("HTML 文件 (*.html);;Markdown 文件 (*.md);;JSON 文件 (*.json);;CSV 文件 (*.csv)"));
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
        std::string json = "{\"error\":\"no compare data\"}";
        if (ctx.hasCompareBundle)
            json = ctx.compareBundle.toJson();
        else if (ctx.hasCompare)
            json = ctx.compare.toJson();
        f.write(QByteArray::fromStdString(json));
    }
    else if (suffix == "csv")
    {
        if (ctx.hasCompareBundle)
            f.write(QByteArray::fromStdString(ctx.compareBundle.toCsv()));
        else
            f.write("error\nno compare data\n");
    }
    else if (suffix == "md")
    {
        QString md;
        md += QString("# %1\n\n").arg(QString::fromStdString(ctx.title));
        md += QString("**Image**: `%1`\n\n").arg(QString::fromStdString(ctx.imagePath));
        if (!ctx.histogramPng.empty())
            md += QString("![Histogram](data:image/png;base64,%1)\n\n")
                      .arg(QString::fromStdString(ctx.histogramPng));
        if (ctx.hasCompareBundle)
        {
            md += "## Compare Report Bundle\n\n```json\n";
            md += QString::fromStdString(ctx.compareBundle.toJson());
            md += "\n```\n\n";
            for (size_t i = 0; i < ctx.compareBundle.targets.size(); ++i)
            {
                if (i >= ctx.compareDiffPngs.size() || ctx.compareDiffPngs[i].empty())
                    continue;
                md += QString("### Diff: %1\n\n![Diff](data:image/png;base64,%2)\n\n")
                          .arg(QString::fromStdString(ctx.compareBundle.targets[i].path),
                               QString::fromStdString(ctx.compareDiffPngs[i]));
            }
        }
        else if (ctx.hasCompare)
        {
            md += "## Compare Report\n\n```json\n";
            md += QString::fromStdString(ctx.compare.toJson());
            md += "\n```\n";
            if (!ctx.compareDiffPng.empty())
                md += QString("\n![Diff](data:image/png;base64,%1)\n")
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
        paths = m_thumbnailPanel->visiblePaths();
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->pathList();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("导出图片"), tr("请先打开一个图片目录。"));
        return;
    }

    ExportDialog dlg(this);
    dlg.setSources(paths);
    dlg.setWindowTitle(tr("导出图片 — %1 张").arg(paths.size()));
    dlg.exec();
}
