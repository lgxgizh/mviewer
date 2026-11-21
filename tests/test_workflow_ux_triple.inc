// Browse workflow: focus/compare across three real directories, then restore one column.
void workflow22_triple_browse()
{
    const AppState before = AppState::load();
    AppState().save();
    QTemporaryDir fixture;
    CHECK(fixture.isValid(), "triple browse workflow has a temporary fixture");
    if (!fixture.isValid())
    {
        before.save();
        return;
    }
    QStringList paths;
    for (const QString &name :
         {QStringLiteral("left"), QStringLiteral("middle"), QStringLiteral("right")})
    {
        QDir dir(fixture.path());
        dir.mkpath(name);
        const QString path = dir.filePath(name + QStringLiteral("/a.png"));
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::blue);
        image.save(path);
        paths.append(path);
    }
    {
        MainWindow window;
        pump(40);
        window.openExternalTargets({QFileInfo(paths[0]).absolutePath()});
        window.setBrowseColumnCount(3);
        window.openRightDirectory(QFileInfo(paths[1]).absolutePath());
        window.openThirdDirectory(QFileInfo(paths[2]).absolutePath());
        auto *left = window.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
        auto *middle = window.secondaryThumbnailPanel();
        auto *third = window.tertiaryThumbnailPanel();
        QElapsedTimer timeout;
        timeout.start();
        while (timeout.elapsed() < 8000 &&
               (left->rowForPath(paths[0]) < 0 || middle->rowForPath(paths[1]) < 0 ||
                third->rowForPath(paths[2]) < 0))
            pump(40);
        CHECK(window.browseColumnCount() == 3 && third->rowForPath(paths[2]) >= 0,
              "three-column workflow loads independent browse panes");
        auto *model = window.selectionModel();
        using Pane = SelectionModel::PaneSlot;
        left->selectPath(paths[0]);
        middle->selectPath(paths[1]);
        third->selectPath(paths[2]);
        window.setActiveBrowsePane(Pane::Tertiary);
        CHECK(window.activeThumbnailPanel() == third && model->selection().size() == 3,
              "third focus and the comparison union agree");
        window.setBrowseColumnCount(2);
        CHECK(window.activeThumbnailPanel() != third && model->selection().size() == 2,
              "closing third restores two-pane workflow without stale picks");
        window.setBrowseColumnCount(1);
        CHECK(model->selection() == QStringList({paths[0]}),
              "returning to single browse retains the primary selection");
    }
    before.save();
}
