#include "library/releasescamp/bandcampservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

#include "library/releases/releasesprefs.h"
#include "library/trackcollectionmanager.h"
#include "moc_bandcampservice.cpp"

namespace mixxx::library::releasescamp {

namespace {

constexpr int kSearchLimit = 50;
constexpr int kResultLimit = 25;
constexpr int kMaximumConcurrentDetails = 6;
const QUrl kSearchUrl(QStringLiteral(
        "https://bandcamp.com/api/bcsearch_public_api/1/autocomplete_elastic"));

QNetworkRequest requestFor(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("Mixxx Bandcamp releases-camp"));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    return request;
}

QString jsonString(const QJsonObject& object, const QString& key) {
    return object.value(key).toString().trimmed();
}

QJsonObject trackInfoFor(const QJsonObject& data) {
    const auto trackInfo = data.value(QStringLiteral("trackinfo")).toArray();
    if (!trackInfo.isEmpty() && trackInfo.first().isObject()) {
        return trackInfo.first().toObject();
    }
    return {};
}

qint64 jsonInt(const QJsonObject& object, const QString& key, qint64 fallback = -1) {
    const auto value = object.value(key);
    return value.isDouble() ? value.toInteger() : fallback;
}

std::optional<BandcampPriceKind> priceKindFor(const QJsonObject& data) {
    const auto current = data.value(QStringLiteral("current")).toObject();
    const auto downloadPreference = jsonInt(current, QStringLiteral("download_pref"),
            jsonInt(data, QStringLiteral("download_pref")));
    const auto minimumPrice = current.value(QStringLiteral("minimum_price"))
                                      .toDouble(data.value(QStringLiteral("minimum_price")).toDouble(-1));
    const auto trackInfo = trackInfoFor(data);
    const auto hasFreeDownload = trackInfo.value(QStringLiteral("has_free_download")).toBool() ||
            trackInfo.value(QStringLiteral("free_album_download")).toBool();
    const auto freeDownloadPage = jsonString(data, QStringLiteral("freeDownloadPage"));
    if (downloadPreference == 1 && (hasFreeDownload || !freeDownloadPage.isEmpty())) {
        return BandcampPriceKind::Free;
    }
    if (downloadPreference == 2 && minimumPrice == 0.0 && !freeDownloadPage.isEmpty()) {
        return BandcampPriceKind::NameYourPriceZero;
    }
    return std::nullopt;
}

} // namespace

BandcampService::BandcampService(QObject* parent,
        UserSettingsPointer config,
        TrackCollectionManager* trackCollectionManager)
        : QObject(parent),
          m_pConfig(std::move(config)),
          m_pDownloader(std::make_unique<mixxx::library::releases::ReleasesService>(
                  nullptr,
                  m_pConfig,
                  trackCollectionManager,
                  mixxx::library::releases::ReleaseProvider::Bandcamp,
                  mixxx::library::releases::prefs::kBandcampDownloadDirectoryConfigKey,
                  QStringLiteral("releases-camp"))) {
    connect(m_pDownloader.get(),
            &mixxx::library::releases::ReleasesService::resultUpdated,
            this,
            &BandcampService::resultUpdated);
    connect(m_pDownloader.get(),
            &mixxx::library::releases::ReleasesService::loadReady,
            this,
            &BandcampService::loadReady);
    connect(m_pDownloader.get(),
            &mixxx::library::releases::ReleasesService::error,
            this,
            &BandcampService::error);
}

BandcampService::~BandcampService() {
    cancel();
}

QList<BandcampSearchCandidate> BandcampService::parseSearchJson(const QByteArray& data) {
    QList<BandcampSearchCandidate> candidates;
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        return candidates;
    }
    const auto autoObject = document.object().value(QStringLiteral("auto")).toObject();
    const auto entries = autoObject.value(QStringLiteral("results")).toArray();
    QSet<QString> seen;
    for (const auto& value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        if (jsonString(object, QStringLiteral("type")) != QStringLiteral("t")) {
            continue;
        }
        BandcampSearchCandidate candidate;
        const auto idValue = object.value(QStringLiteral("id"));
        candidate.id = idValue.isDouble()
                ? QString::number(idValue.toInteger())
                : idValue.toString().trimmed();
        candidate.title = jsonString(object, QStringLiteral("name"));
        candidate.artist = jsonString(object, QStringLiteral("band_name"));
        candidate.album = jsonString(object, QStringLiteral("album_name"));
        candidate.url = jsonString(object, QStringLiteral("item_url_path"));
        candidate.thumbnailUrl = jsonString(object, QStringLiteral("img"));
        const QUrl url(candidate.url);
        if (candidate.id.isEmpty() || candidate.id == QStringLiteral("0") ||
                candidate.id == QStringLiteral("-1") ||
                candidate.title.isEmpty() || !url.isValid() ||
                url.host().isEmpty() || !url.path().contains(QStringLiteral("/track/")) ||
                seen.contains(candidate.url)) {
            continue;
        }
        seen.insert(candidate.url);
        candidates.append(candidate);
        if (candidates.size() >= kSearchLimit) {
            break;
        }
    }
    return candidates;
}

QList<BandcampAlbumCandidate> BandcampService::parseAlbumSearchJson(const QByteArray& data) {
    QList<BandcampAlbumCandidate> albums;
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        return albums;
    }
    const auto entries = document.object().value(QStringLiteral("auto")).toObject()
                                 .value(QStringLiteral("results")).toArray();
    QSet<QString> seen;
    for (const auto& value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        if (jsonString(object, QStringLiteral("type")) != QStringLiteral("a")) {
            continue;
        }
        BandcampAlbumCandidate album;
        album.title = jsonString(object, QStringLiteral("name"));
        album.artist = jsonString(object, QStringLiteral("band_name"));
        album.url = jsonString(object, QStringLiteral("item_url_path"));
        album.thumbnailUrl = jsonString(object, QStringLiteral("img"));
        const QUrl url(album.url);
        if (album.title.isEmpty() || !url.isValid() || url.host().isEmpty() ||
                !url.path().contains(QStringLiteral("/album/")) || seen.contains(album.url)) {
            continue;
        }
        seen.insert(album.url);
        albums.append(album);
    }
    return albums;
}

QList<BandcampSearchResult> BandcampService::parseAlbumPage(
        const QByteArray& data,
        const BandcampAlbumCandidate& album) {
    QList<BandcampSearchResult> results;
    const QRegularExpression expression(
            QStringLiteral("data-tralbum\\s*=\\s*([\\\"'])(.*?)\\1"),
            QRegularExpression::DotMatchesEverythingOption);
    const auto match = expression.match(QString::fromUtf8(data));
    if (!match.hasMatch()) {
        return results;
    }
    const auto jsonText = QTextDocumentFragment::fromHtml(match.captured(2)).toPlainText();
    const auto document = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!document.isObject()) {
        return results;
    }
    const auto dataObject = document.object();
    const auto priceKind = priceKindFor(dataObject);
    if (!priceKind) {
        return results;
    }
    const auto current = dataObject.value(QStringLiteral("current")).toObject();
    const auto albumTitle = jsonString(current, QStringLiteral("title")).isEmpty()
            ? album.title
            : jsonString(current, QStringLiteral("title"));
    const auto albumUrl = QUrl(album.url);
    for (const auto& value : dataObject.value(QStringLiteral("trackinfo")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto track = value.toObject();
        const auto id = jsonInt(track, QStringLiteral("track_id"),
                jsonInt(track, QStringLiteral("id")));
        const auto titleLink = jsonString(track, QStringLiteral("title_link"));
        const auto url = albumUrl.resolved(QUrl(titleLink));
        if (id <= 0 || titleLink.isEmpty() || !url.isValid() || url.host().isEmpty()) {
            continue;
        }
        BandcampSearchResult result;
        result.priceKind = *priceKind;
        result.release.key = QStringLiteral("bandcamp-%1").arg(id);
        result.release.title = jsonString(track, QStringLiteral("title"));
        result.release.uploader = jsonString(track, QStringLiteral("artist"));
        result.release.album = albumTitle;
        result.release.webpageUrl = url.toString();
        result.release.thumbnailUrl = album.thumbnailUrl;
        result.release.durationSeconds = qRound64(track.value(QStringLiteral("duration")).toDouble());
        result.release.provider = mixxx::library::releases::ReleaseProvider::Bandcamp;
        results.append(result);
    }
    return results;
}

std::optional<BandcampSearchResult> BandcampService::parseTrackPage(
        const QByteArray& data,
        const BandcampSearchCandidate& candidate) {
    const QRegularExpression expression(
            QStringLiteral("data-tralbum\\s*=\\s*([\\\"'])(.*?)\\1"),
            QRegularExpression::DotMatchesEverythingOption);
    const auto match = expression.match(QString::fromUtf8(data));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    const auto jsonText = QTextDocumentFragment::fromHtml(match.captured(2)).toPlainText();
    const auto document = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!document.isObject()) {
        return std::nullopt;
    }
    const auto dataObject = document.object();
    const auto trackInfo = trackInfoFor(dataObject);
    const auto priceKind = priceKindFor(dataObject);
    if (!priceKind) {
        return std::nullopt;
    }

    BandcampSearchResult result;
    result.priceKind = *priceKind;
    result.release.key = QStringLiteral("bandcamp-%1").arg(candidate.id);
    result.release.title = candidate.title;
    result.release.uploader = candidate.artist;
    result.release.album = candidate.album;
    result.release.webpageUrl = candidate.url;
    result.release.thumbnailUrl = candidate.thumbnailUrl;
    result.release.durationSeconds = qRound64(trackInfo.value(QStringLiteral("duration")).toDouble());
    result.release.provider = mixxx::library::releases::ReleaseProvider::Bandcamp;
    return result;
}

bool BandcampService::matchesFilter(BandcampPriceKind kind, BandcampPriceFilter filter) {
    return filter == BandcampPriceFilter::Both ||
            (filter == BandcampPriceFilter::FreeOnly && kind == BandcampPriceKind::Free) ||
            (filter == BandcampPriceFilter::NameYourPriceZeroOnly &&
                    kind == BandcampPriceKind::NameYourPriceZero);
}

void BandcampService::search(const QString& query, BandcampPriceFilter filter) {
    const auto trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    resetNetwork();
    m_filter = filter;
    m_query = trimmed;
    m_candidates.clear();
    m_results.clear();
    m_nextCandidate = 0;
    m_completedCandidates = 0;
    m_failedCandidates = 0;
    m_albumCandidate = {};

    QJsonObject body{{QStringLiteral("search_text"), trimmed},
            {QStringLiteral("search_filter"), QStringLiteral("t")},
            {QStringLiteral("full_page"), true},
            {QStringLiteral("fan_id"), QJsonValue::Null}};
    QNetworkRequest request = requestFor(kSearchUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_searchReply = m_network.post(request,
            QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_searchReply,
            &QNetworkReply::finished,
            this,
            &BandcampService::slotSearchFinished);
    emit searchStarted();
}

void BandcampService::slotSearchFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_searchReply) {
        return;
    }
    m_searchReply.clear();
    const auto data = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        emit this->error(tr("The Bandcamp search failed: %1").arg(reply->errorString()));
        return;
    }
    m_candidates = parseSearchJson(data);

    // Bandcamp ranks tracks globally. An album title can therefore return
    // only a handful of its tracks (and omit a valid track entirely). Look up
    // an exact album match as a fallback and expose its tracks as normal track
    // results. The UI remains track-only; this merely makes album-title
    // searches complete.
    QJsonObject body{{QStringLiteral("search_text"), m_query},
            {QStringLiteral("search_filter"), QStringLiteral("a")},
            {QStringLiteral("full_page"), true},
            {QStringLiteral("fan_id"), QJsonValue::Null}};
    QNetworkRequest request = requestFor(kSearchUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_albumSearchReply = m_network.post(request,
            QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_albumSearchReply,
            &QNetworkReply::finished,
            this,
            &BandcampService::slotAlbumSearchFinished);
}

void BandcampService::slotAlbumSearchFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_albumSearchReply) {
        return;
    }
    m_albumSearchReply.clear();
    const auto data = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error == QNetworkReply::NoError) {
        const auto albums = parseAlbumSearchJson(data);
        for (const auto& album : albums) {
            if (album.title.compare(m_query, Qt::CaseInsensitive) != 0) {
                continue;
            }
            m_albumCandidate = album;
            m_albumReply = m_network.get(requestFor(QUrl(album.url)));
            connect(m_albumReply,
                    &QNetworkReply::finished,
                    this,
                    &BandcampService::slotAlbumFinished);
            return;
        }
    }
    startDetails();
}

void BandcampService::slotAlbumFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_albumReply) {
        return;
    }
    m_albumReply.clear();
    const auto data = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error == QNetworkReply::NoError) {
        for (const auto& result : parseAlbumPage(data, m_albumCandidate)) {
            if (!matchesFilter(result.priceKind, m_filter) || m_results.size() >= kResultLimit) {
                break;
            }
            bool duplicate = false;
            for (const auto& existing : m_results) {
                duplicate |= existing.release.key == result.release.key;
            }
            if (!duplicate) {
                m_results.append(result);
            }
        }
    }
    startDetails();
}

void BandcampService::startDetails() {
    startNextDetails();
}

void BandcampService::startNextDetails() {
    while (m_detailReplies.size() < kMaximumConcurrentDetails &&
            m_nextCandidate < m_candidates.size() && m_results.size() < kResultLimit) {
        const auto index = m_nextCandidate++;
        QNetworkRequest request = requestFor(QUrl(m_candidates.at(index).url));
        auto* reply = m_network.get(request);
        m_detailReplies.insert(reply, index);
        connect(reply,
                &QNetworkReply::finished,
                this,
                &BandcampService::slotDetailFinished);
    }
    if (m_detailReplies.isEmpty() &&
            (m_nextCandidate >= m_candidates.size() || m_results.size() >= kResultLimit)) {
        finishDetails();
    }
}

void BandcampService::slotDetailFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || !m_detailReplies.contains(reply)) {
        return;
    }
    const auto index = m_detailReplies.take(reply);
    const auto error = reply->error();
    const auto data = reply->readAll();
    reply->deleteLater();
    ++m_completedCandidates;
    if (error != QNetworkReply::NoError) {
        ++m_failedCandidates;
    } else if (const auto parsed = parseTrackPage(data, m_candidates.at(index)); parsed &&
            matchesFilter(parsed->priceKind, m_filter)) {
        bool duplicate = false;
        for (const auto& result : m_results) {
            duplicate |= result.release.key == parsed->release.key;
        }
        if (!duplicate) {
            m_results.append(*parsed);
        }
    }
    if (m_results.size() >= kResultLimit) {
        finishDetails();
        return;
    }
    if (m_completedCandidates >= m_candidates.size()) {
        finishDetails();
        return;
    }
    startNextDetails();
}

void BandcampService::finishDetails() {
    for (auto* reply : m_detailReplies.keys()) {
        reply->abort();
        reply->deleteLater();
    }
    m_detailReplies.clear();
    if (m_results.isEmpty() && m_failedCandidates == m_completedCandidates &&
            m_completedCandidates > 0) {
        emit error(tr("The Bandcamp track details could not be verified."));
        return;
    }
    emit searchFinished(m_results);
}

void BandcampService::resetNetwork() {
    if (m_searchReply) {
        m_searchReply->abort();
        m_searchReply->deleteLater();
        m_searchReply.clear();
    }
    if (m_albumSearchReply) {
        m_albumSearchReply->abort();
        m_albumSearchReply->deleteLater();
        m_albumSearchReply.clear();
    }
    if (m_albumReply) {
        m_albumReply->abort();
        m_albumReply->deleteLater();
        m_albumReply.clear();
    }
    for (auto* reply : m_detailReplies.keys()) {
        reply->abort();
        reply->deleteLater();
    }
    m_detailReplies.clear();
}

void BandcampService::cancel() {
    resetNetwork();
    if (m_pDownloader) {
        m_pDownloader->cancel();
    }
}

void BandcampService::requestLoad(const BandcampSearchResult& result,
        const QString& group,
        bool play) {
    m_pDownloader->requestLoad(result.release, group, play);
}

bool BandcampService::isCached(const QString& key) const {
    return m_pDownloader->isCached(key);
}

QString BandcampService::thumbnailPath(const QString& key) const {
    return m_pDownloader->thumbnailPath(key);
}

bool BandcampService::cacheThumbnail(const QString& key, const QImage& image) {
    return m_pDownloader->cacheThumbnail(key, image);
}

} // namespace mixxx::library::releasescamp
