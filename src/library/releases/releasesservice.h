#pragma once

#include <QList>
#include <QProcess>
#include <QTimer>

#include "library/coverart.h"
#include "library/releases/releasescatalog.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"

class TrackCollectionManager;

namespace mixxx::library::releases {

struct ReleaseSearchResult {
    QString key;
    QString title;
    QString uploader;
    QString webpageUrl;
    QString thumbnailUrl;
    QString thumbnailPath;
    qint64 durationSeconds{0};
    QString mediaPath;
    int progress{-1};
    QString status;
    bool cached{false};
};

class ReleasesService final : public QObject {
    Q_OBJECT
  public:
    ReleasesService(QObject* parent,
            UserSettingsPointer config,
            TrackCollectionManager* trackCollectionManager);
    ~ReleasesService() override;

    void search(const QString& query, bool musicOnly);
    void requestLoad(const ReleaseSearchResult& result,
            const QString& group = QString(),
            bool play = false);
    void cancel();
    bool isCached(const QString& key) const;
    QString thumbnailPath(const QString& key) const;
    bool cacheThumbnail(const QString& key, const QImage& image);

    static QList<ReleaseSearchResult> parseSearchJson(const QByteArray& data);
    static int progressPercent(qint64 downloaded, qint64 total);

  signals:
    void searchStarted();
    void searchFinished(const QList<mixxx::library::releases::ReleaseSearchResult>& results);
    void resultUpdated(const QString& key,
            int progress,
            const QString& status);
    void loadReady(TrackPointer track, const QString& group, bool play);
    void error(const QString& message);

  private slots:
    void slotReadyReadStandardOutput();
    void slotReadyReadStandardError();
    void slotSearchFinished(int exitCode, QProcess::ExitStatus status);
    void slotDownloadFinished(int exitCode, QProcess::ExitStatus status);

  private:
    enum class Operation { None,
        Search,
        Download };
    struct PendingLoad {
        ReleaseSearchResult result;
        QString group;
        bool play{false};
    };

    QString helperPath() const;
    QStringList cookieArguments() const;
    bool ensureDownloadDirectory(QString* path);
    void startNextDownload();
    void startDownload(const PendingLoad& request);
    void handleDownloadLine(const QString& line);
    void processDownloadBuffer(QByteArray* buffer);
    void finishDownload(const PendingLoad& request, const QString& mediaPath);
    TrackPointer trackForRecord(const ReleaseSearchResult& result,
            const QString& mediaPath);
    void resetProcess();

    UserSettingsPointer m_pConfig;
    TrackCollectionManager* const m_pTrackCollectionManager;
    ReleasesCatalog m_catalog;
    QProcess m_process;
    Operation m_operation{Operation::None};
    QByteArray m_processOutput;
    QByteArray m_processErrorOutput;
    QString m_downloadErrorDetail;
    QString m_activeKey;
    QString m_activeMediaPath;
    QList<PendingLoad> m_downloadQueue;
    QList<PendingLoad> m_activeLoads;
    bool m_cancelRequested{false};
};

} // namespace mixxx::library::releases

Q_DECLARE_METATYPE(mixxx::library::releases::ReleaseSearchResult)
Q_DECLARE_METATYPE(QList<mixxx::library::releases::ReleaseSearchResult>)
