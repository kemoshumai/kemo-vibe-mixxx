#include "library/releases/releasesservice.h"

#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>

#include "library/releases/releasesprefs.h"
#include "library/trackcollectionmanager.h"
#include "moc_releasesservice.cpp"
#include "track/track.h"
#include "track/trackref.h"

namespace mixxx::library::releases {

namespace {

constexpr int kSearchLimit = 25;
const QString kProgressPrefix = QStringLiteral("RELEASES_PROGRESS");
const QString kCompletePrefix = QStringLiteral("RELEASES_COMPLETE");

QString firstString(const QJsonObject& object, std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        const auto value = object.value(QString::fromLatin1(name)).toString();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString thumbnailUrl(const QJsonObject& object) {
    QString result;
    for (const auto& value : object.value(QStringLiteral("thumbnails")).toArray()) {
        const auto url = value.toObject().value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            result = url;
        }
    }
    return result;
}

QString searchErrorDetail(const QByteArray& output) {
    const auto lines = QString::fromUtf8(output).split(QChar('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        auto line = it->trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive)) {
            line = line.mid(6).trimmed();
        }
        constexpr int kMaximumDetailLength = 320;
        if (line.size() > kMaximumDetailLength) {
            line = line.left(kMaximumDetailLength) + QChar(0x2026);
        }
        return line;
    }
    return {};
}

} // anonymous namespace

ReleasesService::ReleasesService(QObject* parent,
        UserSettingsPointer config,
        TrackCollectionManager* trackCollectionManager,
        ReleaseProvider provider,
        ConfigKey downloadDirectoryConfigKey,
        QString catalogName)
        : QObject(parent),
          m_pConfig(std::move(config)),
          m_pTrackCollectionManager(trackCollectionManager),
          m_catalog(m_pConfig->getSettingsPath(), std::move(catalogName)),
          m_provider(provider),
          m_downloadDirectoryConfigKey(std::move(downloadDirectoryConfigKey)) {
    m_catalog.load();
    connect(&m_process,
            &QProcess::readyReadStandardOutput,
            this,
            &ReleasesService::slotReadyReadStandardOutput);
    connect(&m_process,
            &QProcess::readyReadStandardError,
            this,
            &ReleasesService::slotReadyReadStandardError);
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (m_operation == Operation::Search) {
                    slotSearchFinished(exitCode, status);
                } else if (m_operation == Operation::Download) {
                    slotDownloadFinished(exitCode, status);
                }
            });
}

ReleasesService::~ReleasesService() {
    cancel();
}

QString ReleasesService::helperPath() const {
    const auto configured = m_pConfig->getValueString(prefs::kHelperPathConfigKey);
    if (!configured.isEmpty()) {
        return configured;
    }
#ifdef Q_OS_WIN
    const auto executable = QStandardPaths::findExecutable(QStringLiteral("yt-dlp.exe"));
    if (!executable.isEmpty()) {
        return executable;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

QStringList ReleasesService::cookieArguments() const {
    if (!m_pConfig->getValue(prefs::kUseBrowserCookiesConfigKey, false)) {
        return {};
    }
    const auto browser = m_pConfig->getValue(
            prefs::kCookieBrowserConfigKey,
            QStringLiteral("chrome"));
    if (browser.isEmpty()) {
        return {};
    }
    auto selector = browser;
    const auto profile = m_pConfig->getValueString(prefs::kCookieProfileConfigKey);
    if (!profile.isEmpty()) {
        selector += QStringLiteral(":") + profile;
    }
    return {QStringLiteral("--cookies-from-browser"), selector};
}

void ReleasesService::search(const QString& query, bool musicOnly) {
    if (m_operation == Operation::Download) {
        emit error(tr("Finish the current release download before searching again."));
        return;
    }
    cancel();
    const auto helper = helperPath();
    if (helper.isEmpty()) {
        emit error(tr("The releases helper executable was not found."));
        return;
    }

    const auto searchTerms = musicOnly
            ? query + QStringLiteral(" music")
            : query;
    const auto input = QStringLiteral("ytsearch%1:%2").arg(kSearchLimit).arg(searchTerms);

    m_operation = Operation::Search;
    m_processOutput.clear();
    m_processErrorOutput.clear();
    QStringList arguments{QStringLiteral("--ignore-config"),
            QStringLiteral("--no-warnings"),
            QStringLiteral("--dump-single-json"),
            QStringLiteral("--flat-playlist"),
            QStringLiteral("--playlist-end"),
            QString::number(kSearchLimit),
            QStringLiteral("--skip-download")};
    arguments.append(cookieArguments());
    arguments.append(input);
    emit searchStarted();
    m_process.start(helper, arguments);
    if (!m_process.waitForStarted(2000)) {
        resetProcess();
        emit error(tr("The releases helper executable could not be started."));
    }
}

QList<ReleaseSearchResult> ReleasesService::parseSearchJson(const QByteArray& data) {
    QList<ReleaseSearchResult> results;
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        return results;
    }
    for (const auto& value : document.object().value(QStringLiteral("entries")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        ReleaseSearchResult result;
        const auto extractor = firstString(object, {"extractor_key", "ie_key", "extractor"});
        const auto id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            continue;
        }
        result.key = (extractor.isEmpty() ? QStringLiteral("youtube") : extractor) +
                QStringLiteral("-") + id;
        result.title = firstString(object, {"title", "fulltitle"});
        result.uploader = firstString(object, {"uploader", "channel"});
        result.webpageUrl = firstString(object, {"webpage_url", "original_url", "url"});
        const QUrl resultUrl(result.webpageUrl);
        if (result.webpageUrl.isEmpty() || resultUrl.scheme().isEmpty()) {
            result.webpageUrl = QStringLiteral("https://www.youtube.com/watch?v=") + id;
        }
        result.thumbnailUrl = QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id);
        if (extractor.compare(QStringLiteral("Youtube"), Qt::CaseInsensitive) != 0) {
            result.thumbnailUrl = thumbnailUrl(object);
        }
        result.durationSeconds = object.value(QStringLiteral("duration")).toInteger();
        results.append(result);
    }
    return results;
}

int ReleasesService::progressPercent(qint64 downloaded, qint64 total) {
    if (downloaded < 0 || total <= 0) {
        return -1;
    }
    return qBound(0, static_cast<int>((downloaded * 100) / total), 100);
}

void ReleasesService::slotReadyReadStandardOutput() {
    m_processOutput.append(m_process.readAllStandardOutput());
    if (m_operation != Operation::Download) {
        return;
    }
    processDownloadBuffer(&m_processOutput);
}

void ReleasesService::slotReadyReadStandardError() {
    m_processErrorOutput.append(m_process.readAllStandardError());
    if (m_operation != Operation::Download) {
        return;
    }
    processDownloadBuffer(&m_processErrorOutput);
}

void ReleasesService::processDownloadBuffer(QByteArray* buffer) {
    while (true) {
        const auto newline = buffer->indexOf('\n');
        if (newline < 0) {
            break;
        }
        const auto line = QString::fromUtf8(buffer->left(newline)).trimmed();
        buffer->remove(0, newline + 1);
        handleDownloadLine(line);
    }
}

void ReleasesService::slotSearchFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_cancelRequested) {
        m_cancelRequested = false;
        resetProcess();
        return;
    }
    const auto data = m_processOutput + m_process.readAllStandardOutput();
    const auto errorOutput = m_processErrorOutput + m_process.readAllStandardError();
    const auto results = parseSearchJson(data);
    resetProcess();
    if (results.isEmpty() && (status != QProcess::NormalExit || exitCode != 0)) {
        const auto detail = searchErrorDetail(errorOutput);
        emit error(detail.isEmpty()
                        ? tr("The releases search failed. Open Settings and check the helper.")
                        : tr("The releases search failed: %1").arg(detail));
        return;
    }
    emit searchFinished(results);
}

bool ReleasesService::ensureDownloadDirectory(QString* path) {
    const auto& directoryKey = m_downloadDirectoryConfigKey.isValid()
            ? m_downloadDirectoryConfigKey
            : prefs::kDownloadDirectoryConfigKey;
    auto directory = m_pConfig->getValueString(directoryKey);
    if (directory.isEmpty()) {
        const auto defaultDirectoryName = m_provider == ReleaseProvider::Bandcamp
                ? QStringLiteral("Releases Camp")
                : QStringLiteral("Releases");
        directory = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) +
                QStringLiteral("/Mixxx/") + defaultDirectoryName;
        if (directory.startsWith(QStringLiteral("/Mixxx"))) {
            directory = QDir(m_pConfig->getSettingsPath()).filePath(defaultDirectoryName);
        }
    }
    QDir dir(directory);
    if (!dir.exists() && !QDir().mkpath(directory)) {
        return false;
    }
    *path = dir.absolutePath();
    return true;
}

void ReleasesService::requestLoad(const ReleaseSearchResult& result,
        const QString& group,
        bool play) {
    const auto cached = m_catalog.record(result.key);
    if (cached && QFileInfo::exists(cached->mediaPath)) {
        auto cachedResult = result;
        cachedResult.mediaPath = cached->mediaPath;
        cachedResult.cached = true;
        const auto track = trackForRecord(cachedResult, cached->mediaPath);
        if (track) {
            emit loadReady(track, group, play);
        }
        return;
    }
    if (m_activeKey == result.key) {
        m_activeLoads.append({result, group, play});
        return;
    }
    for (int i = 0; i < m_downloadQueue.size(); ++i) {
        if (m_downloadQueue.at(i).result.key == result.key) {
            m_downloadQueue.append({result, group, play});
            return;
        }
    }
    m_downloadQueue.append({result, group, play});
    startNextDownload();
}

void ReleasesService::startNextDownload() {
    if (m_operation == Operation::Download || m_downloadQueue.isEmpty()) {
        return;
    }
    const auto request = m_downloadQueue.takeFirst();
    startDownload(request);
}

void ReleasesService::startDownload(const PendingLoad& request) {
    const auto helper = helperPath();
    QString directory;
    if (helper.isEmpty() || !ensureDownloadDirectory(&directory)) {
        emit error(helper.isEmpty()
                        ? tr("The releases helper executable was not found.")
                        : tr("The releases download directory is not available."));
        return;
    }
    m_operation = Operation::Download;
    m_processOutput.clear();
    m_processErrorOutput.clear();
    m_downloadErrorDetail.clear();
    m_activeKey = request.result.key;
    m_activeLoads = {request};
    m_activeMediaPath.clear();
    emit resultUpdated(request.result.key, 0, tr("Queued"));

    QStringList arguments{QStringLiteral("--ignore-config"),
            QStringLiteral("--no-warnings"),
            QStringLiteral("--encoding"),
            QStringLiteral("utf-8"),
            QStringLiteral("--no-playlist"),
            QStringLiteral("--no-overwrites"),
            QStringLiteral("--format"),
            QStringLiteral("bestaudio/best"),
            QStringLiteral("--paths"),
            directory,
            QStringLiteral("--output"),
            QStringLiteral("%(title).160B [%(extractor_key)s-%(id)s].%(ext)s"),
            QStringLiteral("--newline"),
            QStringLiteral("--progress"),
            QStringLiteral("--progress-template"),
            QStringLiteral("download:RELEASES_PROGRESS\t%(info.id)s\t"
                           "%(progress.downloaded_bytes)s\t%(progress.total_bytes)s\t"
                           "%(progress.total_bytes_estimate)s"),
            QStringLiteral("--print"),
            QStringLiteral("after_move:RELEASES_COMPLETE\t%(id)s\t%(filepath)s")};
    if (m_provider == ReleaseProvider::YouTube) {
        const auto formatIndex = arguments.indexOf(QStringLiteral("--format"));
        arguments.insert(formatIndex, QStringLiteral("--extractor-args"));
        arguments.insert(formatIndex + 1, QStringLiteral("youtube:player_client=android"));
    }
    arguments.append(cookieArguments());
    arguments.append(request.result.webpageUrl);
    m_process.start(helper, arguments);
    if (!m_process.waitForStarted(2000)) {
        resetProcess();
        emit resultUpdated(request.result.key, -1, tr("Failed to start"));
        m_activeKey.clear();
        m_activeLoads.clear();
        startNextDownload();
    }
}

void ReleasesService::handleDownloadLine(const QString& line) {
    const auto fields = line.split(QChar('\t'));
    if (fields.size() >= 5 && fields.first() == kProgressPrefix) {
        bool downloadedOk = false;
        bool totalOk = false;
        bool estimateOk = false;
        const auto downloaded = fields.at(2).toLongLong(&downloadedOk);
        auto total = fields.at(3).toLongLong(&totalOk);
        if (!totalOk || total <= 0) {
            total = fields.at(4).toLongLong(&estimateOk);
        }
        const auto progress = progressPercent(downloadedOk ? downloaded : -1,
                (totalOk || estimateOk) ? total : -1);
        emit resultUpdated(m_activeKey, progress, tr("Downloading"));
    } else if (fields.size() >= 3 && fields.first() == kCompletePrefix) {
        const auto filepath = fields.at(2).trimmed();
        m_activeMediaPath = filepath;
        emit resultUpdated(m_activeKey, 100, tr("Saving"));
    } else if (!line.isEmpty()) {
        m_downloadErrorDetail = line;
    }
}

void ReleasesService::slotDownloadFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_operation == Operation::Download) {
        m_processOutput.append(m_process.readAllStandardOutput());
        m_processErrorOutput.append(m_process.readAllStandardError());
        processDownloadBuffer(&m_processOutput);
        processDownloadBuffer(&m_processErrorOutput);
    }
    if (!m_activeLoads.isEmpty() && !QFileInfo::exists(m_activeMediaPath)) {
        QString directory;
        if (ensureDownloadDirectory(&directory)) {
            const auto marker = QStringLiteral("[%1]").arg(
                    m_activeLoads.constFirst().result.key);
            const auto files = QDir(directory).entryInfoList(
                    QDir::Files | QDir::Readable, QDir::Time);
            for (const auto& file : files) {
                if (file.fileName().contains(marker, Qt::CaseInsensitive)) {
                    m_activeMediaPath = file.absoluteFilePath();
                    break;
                }
            }
        }
    }
    if (!m_activeLoads.isEmpty() && status == QProcess::NormalExit && exitCode == 0 &&
            !m_activeMediaPath.isEmpty() && QFileInfo::exists(m_activeMediaPath)) {
        const auto loads = m_activeLoads;
        for (const auto& request : loads) {
            finishDownload(request, m_activeMediaPath);
        }
    } else if (!m_activeKey.isEmpty()) {
        emit resultUpdated(m_activeKey, -1, tr("Failed"));
        auto detail = m_downloadErrorDetail;
        if (detail.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive)) {
            detail = detail.mid(6).trimmed();
        }
        emit error(detail.isEmpty()
                        ? tr("The selected release could not be saved.")
                        : tr("The selected release could not be saved: %1").arg(detail));
    }
    m_activeLoads.clear();
    m_activeKey.clear();
    m_activeMediaPath.clear();
    resetProcess();
    startNextDownload();
}

void ReleasesService::finishDownload(const PendingLoad& request, const QString& mediaPath) {
    ReleaseRecord record;
    record.key = request.result.key;
    record.title = request.result.title;
    record.uploader = request.result.uploader;
    record.webpageUrl = request.result.webpageUrl;
    record.mediaPath = mediaPath;
    record.durationSeconds = request.result.durationSeconds;
    record.thumbnailPath = request.result.thumbnailPath;
    if (record.thumbnailPath.isEmpty()) {
        const auto existing = m_catalog.record(record.key);
        if (existing) {
            record.thumbnailPath = existing->thumbnailPath;
        }
    }
    m_catalog.put(record);
    m_catalog.save();
    const auto track = trackForRecord(request.result, mediaPath);
    if (track) {
        emit resultUpdated(request.result.key, 100, tr("Saved"));
        emit loadReady(track, request.group, request.play);
    }
}

QString ReleasesService::thumbnailPath(const QString& key) const {
    const auto record = m_catalog.record(key);
    if (record && QFileInfo::exists(record->thumbnailPath)) {
        return record->thumbnailPath;
    }
    return m_catalog.thumbnailCachePath(key);
}

bool ReleasesService::cacheThumbnail(const QString& key, const QImage& image) {
    if (key.isEmpty() || image.isNull()) {
        return false;
    }
    const auto path = m_catalog.thumbnailCachePath(key);
    QImageWriter writer(path, QByteArrayLiteral("jpg"));
    if (!writer.write(image)) {
        return false;
    }
    auto record = m_catalog.record(key).value_or(ReleaseRecord{});
    record.key = key;
    record.thumbnailPath = path;
    m_catalog.put(record);
    return m_catalog.save();
}

TrackPointer ReleasesService::trackForRecord(const ReleaseSearchResult& result,
        const QString& mediaPath) {
    if (!m_pTrackCollectionManager) {
        return {};
    }
    auto track = m_pTrackCollectionManager->getOrAddTrack(TrackRef::fromFilePath(mediaPath));
    if (!track) {
        return {};
    }
    if (!result.title.isEmpty()) {
        track->setTitle(result.title);
    }
    if (!result.uploader.isEmpty()) {
        track->setArtist(result.uploader);
    }
    if (!result.album.isEmpty()) {
        track->setAlbum(result.album);
    }
    track->setURL(result.webpageUrl);
    m_pTrackCollectionManager->saveTrack(track);
    return track;
}

void ReleasesService::cancel() {
    if (m_process.state() != QProcess::NotRunning) {
        m_cancelRequested = true;
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    resetProcess();
    m_downloadQueue.clear();
    m_activeLoads.clear();
    m_activeKey.clear();
}

bool ReleasesService::isCached(const QString& key) const {
    const auto record = m_catalog.record(key);
    return record.has_value() && QFileInfo::exists(record->mediaPath);
}

void ReleasesService::resetProcess() {
    m_processOutput.clear();
    m_processErrorOutput.clear();
    m_downloadErrorDetail.clear();
    m_operation = Operation::None;
}

} // namespace mixxx::library::releases
