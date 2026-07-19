#pragma once

#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QString>
#include <optional>

namespace mixxx::library::releases {

struct ReleaseRecord {
    QString key;
    QString title;
    QString uploader;
    QString webpageUrl;
    QString mediaPath;
    QString thumbnailPath;
    qint64 durationSeconds{0};

    QJsonObject toJson() const;
    static ReleaseRecord fromJson(const QJsonObject& object);
};

class ReleasesCatalog final {
  public:
    explicit ReleasesCatalog(const QString& settingsPath,
            QString catalogName = QStringLiteral("releases"));

    bool load();
    bool save() const;
    std::optional<ReleaseRecord> record(const QString& key) const;
    void put(const ReleaseRecord& record);
    void remove(const QString& key);
    QString thumbnailCachePath(const QString& key) const;

  private:
    QString m_filePath;
    QString m_thumbnailDirectory;
    QMap<QString, ReleaseRecord> m_records;
};

} // namespace mixxx::library::releases
