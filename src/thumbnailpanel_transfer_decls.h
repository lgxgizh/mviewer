// Included from inside ThumbnailPanel's private section so thumbnailpanel.h
// stays within the complexity-gate file cap. Ends in private: the members
// declared after the include keep that access. clang-format is off because
// this fragment is not a standalone translation unit.
// clang-format off
public:
    void copyPathsInto(const QStringList &paths, const QString &destinationDirectory);
    void movePathsInto(const QStringList &paths, const QString &destinationDirectory);
    void copySelectedToOtherPane();
    void moveSelectedToOtherPane();
    void showTransferredFiles(const QString &destinationDirectory, const QStringList &createdPaths);

private:
    // Paths just copied or moved into this directory. Merged into the next
    // incremental selection so an async filter still selects them.
    QStringList m_transferSelectPending;
    void publishTransferIfAny(const ICommand *command);
// clang-format on
