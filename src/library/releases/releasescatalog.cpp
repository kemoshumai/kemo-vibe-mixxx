#include "library/releases/releasescatalog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace mixxx::library::releases {

QJsonObject ReleaseRecord::toJson() const {
    return QJsonObject{{QStringLiteral("key"), key},
            {QStringLiteral("title"), title},
            {QStringLiteral("uploader"), uploader},
            {QStringLiteral("webpageUrl"), webpageUrl},
            {QStringLiteral("mediaPath"), mediaPath},
            {QStringLiteral("thumbnailPath"), thumbnailPath},
            {QStringLiteral("duration"), durationSeconds}};
}

ReleaseRecord ReleaseRecord::fromJson(const QJsonObject& object) {
    ReleaseRecord record;
    record.key = object.value(QStringLiteral("key")).toString();
    record.title = object.value(QStringLiteral("title")).toString();
    record.uploader = object.value(QStringLiteral("uploader")).toString();
    record.webpageUrl = object.value(QStringLiteral("webpageUrl")).toString();
    record.mediaPath = object.value(QStringLiteral("mediaPath")).toString();
    record.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    record.durationSeconds = object.value(QStringLiteral("duration")).toInteger();
    return record;
}

ReleasesCatalog::ReleasesCatalog(const QString& settingsPath, QString catalogName)
        : m_filePath(QDir(settingsPath).filePath(catalogName + QStringLiteral("-index.json"))),
          m_thumbnailDirectory(
                  QDir(settingsPath).filePath(catalogName + QStringLiteral("-thumbnails"))) {
}

bool ReleasesCatalog::load() {
    m_records.clear();
    QFile file(m_filePath);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    for (const auto& value : document.object().value(QStringLiteral("records")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto record = ReleaseRecord::fromJson(value.toObject());
        if (!record.key.isEmpty()) {
            m_records.insert(record.key, record);
        }
    }
    return true;
}

bool ReleasesCatalog::save() const {
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QJsonArray records;
    for (const auto& record : m_records) {
        records.append(record.toJson());
    }
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("records"), records}};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

std::optional<ReleaseRecord> ReleasesCatalog::record(const QString& key) const {
    const auto it = m_records.constFind(key);
    if (it == m_records.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

void ReleasesCatalog::put(const ReleaseRecord& record) {
    m_records.insert(record.key, record);
}

void ReleasesCatalog::remove(const QString& key) {
    m_records.remove(key);
}

QString ReleasesCatalog::thumbnailCachePath(const QString& key) const {
    QDir().mkpath(m_thumbnailDirectory);
    return QDir(m_thumbnailDirectory).filePath(key + QStringLiteral(".jpg"));
}

} // namespace mixxx::library::releases
