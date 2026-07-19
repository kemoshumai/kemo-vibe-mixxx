#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <memory>
#include <optional>

#include "library/releases/releasesservice.h"

class TrackCollectionManager;

namespace mixxx::library::releasescamp {

enum class BandcampPriceKind {
    Free,
    NameYourPriceZero,
};

enum class BandcampPriceFilter {
    Both,
    FreeOnly,
    NameYourPriceZeroOnly,
};

struct BandcampSearchCandidate {
    QString id;
    QString title;
    QString artist;
    QString album;
    QString url;
    QString thumbnailUrl;
};

struct BandcampAlbumCandidate {
    QString title;
    QString artist;
    QString url;
    QString thumbnailUrl;
};

struct BandcampSearchResult {
    mixxx::library::releases::ReleaseSearchResult release;
    BandcampPriceKind priceKind{BandcampPriceKind::Free};
};

class BandcampService final : public QObject {
    Q_OBJECT
  public:
    BandcampService(QObject* parent,
            UserSettingsPointer config,
            TrackCollectionManager* trackCollectionManager);
    ~BandcampService() override;

    void search(const QString& query, BandcampPriceFilter filter);
    void requestLoad(const BandcampSearchResult& result,
            const QString& group = QString(),
            bool play = false);
    void cancel();

    bool isCached(const QString& key) const;
    QString thumbnailPath(const QString& key) const;
    bool cacheThumbnail(const QString& key, const QImage& image);

    static QList<BandcampSearchCandidate> parseSearchJson(const QByteArray& data);
    static QList<BandcampAlbumCandidate> parseAlbumSearchJson(const QByteArray& data);
    static QList<BandcampSearchResult> parseAlbumPage(
            const QByteArray& data,
            const BandcampAlbumCandidate& album);
    static std::optional<BandcampSearchResult> parseTrackPage(
            const QByteArray& data,
            const BandcampSearchCandidate& candidate);
    static bool matchesFilter(BandcampPriceKind kind, BandcampPriceFilter filter);

  signals:
    void searchStarted();
    void searchFinished(const QList<mixxx::library::releasescamp::BandcampSearchResult>& results);
    void resultUpdated(const QString& key, int progress, const QString& status);
    void loadReady(TrackPointer track, const QString& group, bool play);
    void error(const QString& message);

  private slots:
    void slotSearchFinished();
    void slotAlbumSearchFinished();
    void slotAlbumFinished();
    void slotDetailFinished();

  private:
    void startDetails();
    void startNextDetails();
    void finishDetails();
    void resetNetwork();

    UserSettingsPointer m_pConfig;
    std::unique_ptr<mixxx::library::releases::ReleasesService> m_pDownloader;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_searchReply;
    QPointer<QNetworkReply> m_albumSearchReply;
    QPointer<QNetworkReply> m_albumReply;
    QHash<QNetworkReply*, int> m_detailReplies;
    QList<BandcampSearchCandidate> m_candidates;
    QList<BandcampSearchResult> m_results;
    BandcampPriceFilter m_filter{BandcampPriceFilter::Both};
    int m_nextCandidate{0};
    int m_completedCandidates{0};
    int m_failedCandidates{0};
    QString m_query;
    BandcampAlbumCandidate m_albumCandidate;
};

} // namespace mixxx::library::releasescamp

Q_DECLARE_METATYPE(mixxx::library::releasescamp::BandcampSearchResult)
Q_DECLARE_METATYPE(QList<mixxx::library::releasescamp::BandcampSearchResult>)
