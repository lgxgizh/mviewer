#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// M19: single source of truth for the in-session workspace shell state.
//
// A workspace is "where the user left off": root directory, open compare
// session, analysis panel visibility/page. Domain::Workspace remains the
// serializable value type; this model is the live UI-facing SSOT that
// MainWindow and panels observe. Only one WorkspaceModel exists per app.
//
// Intentionally tiny: holds state and emits change signals. Serialization
// still goes through WorkspaceSerializer / ProjectSerializer.
class WorkspaceModel : public QObject
{
    Q_OBJECT
  public:
    explicit WorkspaceModel(QObject *parent = nullptr);

    QString rootPath() const
    {
        return m_rootPath;
    }
    QStringList comparedImages() const
    {
        return m_comparedImages;
    }
    QString compareSessionJson() const
    {
        return m_compareSessionJson;
    }
    bool analysisVisible() const
    {
        return m_analysisVisible;
    }
    int analysisPage() const
    {
        return m_analysisPage;
    }
    bool isEmpty() const
    {
        return m_rootPath.isEmpty();
    }

  public slots:
    void setRootPath(const QString &path);
    void setComparedImages(const QStringList &images);
    void setCompareSessionJson(const QString &json);
    void setAnalysisVisible(bool visible);
    void setAnalysisPage(int page);
    void clear();

  signals:
    void rootPathChanged(const QString &path);
    void comparedImagesChanged(const QStringList &images);
    void compareSessionJsonChanged(const QString &json);
    void analysisVisibleChanged(bool visible);
    void analysisPageChanged(int page);
    void workspaceReset();

  private:
    QString m_rootPath;
    QStringList m_comparedImages;
    QString m_compareSessionJson;
    bool m_analysisVisible = true;
    int m_analysisPage = 0;
};
