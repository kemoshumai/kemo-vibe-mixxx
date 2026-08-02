#include <gtest/gtest.h>

#include "library/releases/releasesservice.h"

namespace mixxx::library::releases {

TEST(ReleasesServiceTest, ParsesSearchEntries) {
    const QByteArray json = R"JSON({
        "entries": [
            {
                "extractor_key": "Youtube",
                "id": "abc123",
                "title": "A song",
                "uploader": "An artist",
                "webpage_url": "https://www.youtube.com/watch?v=abc123",
                "duration": 187,
                "thumbnails": [{"url": "https://example.test/thumb.jpg"}]
            },
            {"id": "missing-title"}
        ]
    })JSON";

    const auto results = ReleasesService::parseSearchJson(json);
    ASSERT_EQ(2, results.size());
    EXPECT_EQ(QStringLiteral("Youtube-abc123"), results.at(0).key);
    EXPECT_EQ(QStringLiteral("A song"), results.at(0).title);
    EXPECT_EQ(QStringLiteral("An artist"), results.at(0).uploader);
    EXPECT_EQ(187, results.at(0).durationSeconds);
    EXPECT_EQ(QStringLiteral("https://i.ytimg.com/vi/abc123/mqdefault.jpg"),
            results.at(0).thumbnailUrl);
    EXPECT_EQ(QStringLiteral("youtube-missing-title"), results.at(1).key);
    EXPECT_EQ(QStringLiteral("https://www.youtube.com/watch?v=missing-title"),
            results.at(1).webpageUrl);
}

TEST(ReleasesServiceTest, ParsesSoundCloudSearchEntries) {
    const QByteArray json = R"JSON({
        "entries": [
            {
                "extractor_key": "Soundcloud",
                "id": "ABC123",
                "title": "A SoundCloud track",
                "uploader": "A producer",
                "album": "A release",
                "webpage_url": "http://www.soundcloud.com/Artist/Track/?si=tracking#fragment",
                "duration": 204,
                "thumbnails": [
                    {"url": "https://example.test/small.jpg", "width": 100, "height": 100},
                    {"url": "https://example.test/large.jpg", "width": 500, "height": 500},
                    {"url": "https://example.test/medium.jpg", "width": 300, "height": 300}
                ]
            },
            {
                "title": "Missing ID",
                "webpage_url": "https://soundcloud.com/artist/missing-id"
            },
            {
                "id": "not-soundcloud",
                "title": "Wrong host",
                "webpage_url": "https://example.test/artist/track"
            },
            {
                "extractor_key": "SoundcloudSet",
                "id": "collection",
                "title": "Not a track",
                "webpage_url": "https://soundcloud.com/artist/sets"
            }
        ]
    })JSON";

    const auto results =
            ReleasesService::parseSearchJson(json, ReleaseProvider::SoundCloud);
    ASSERT_EQ(1, results.size());
    const auto& result = results.constFirst();
    EXPECT_EQ(QStringLiteral("soundcloud-abc123"), result.key);
    EXPECT_EQ(QStringLiteral("A SoundCloud track"), result.title);
    EXPECT_EQ(QStringLiteral("A producer"), result.uploader);
    EXPECT_EQ(QStringLiteral("A release"), result.album);
    EXPECT_EQ(QStringLiteral("https://soundcloud.com/Artist/Track"), result.webpageUrl);
    EXPECT_EQ(QStringLiteral("https://example.test/large.jpg"), result.thumbnailUrl);
    EXPECT_EQ(204, result.durationSeconds);
    EXPECT_EQ(ReleaseProvider::SoundCloud, result.provider);
}

TEST(ReleasesServiceTest, DefinesSoundCloudCommandPolicy) {
    const auto policy = ReleasesService::commandPolicy(ReleaseProvider::SoundCloud);

    EXPECT_EQ(QStringLiteral("scsearch25"), policy.searchPrefix);
    EXPECT_FALSE(policy.appendMusicKeyword);
    EXPECT_FALSE(policy.allowBrowserCookies);
    EXPECT_EQ(QStringLiteral("Releases SC"), policy.defaultDownloadDirectoryName);
    EXPECT_EQ(QStringLiteral("soundcloud:formats=hls_aac"),
            policy.downloadExtractorArguments);
    EXPECT_TRUE(policy.downloadFormatSelector.contains(
            QStringLiteral("protocol=m3u8_native")));
    EXPECT_TRUE(policy.downloadFormatSelector.contains(QStringLiteral("acodec^=mp4a")));
    EXPECT_TRUE(policy.downloadFormatSelector.contains(QStringLiteral("abr=160")));
    EXPECT_TRUE(policy.downloadFormatSelector.contains(QStringLiteral("abr=96")));
    EXPECT_TRUE(policy.downloadFormatSelector.contains(
            QStringLiteral("format_note!=?Premium")));
    EXPECT_TRUE(policy.downloadFormatSelector.contains(
            QStringLiteral("format_id!$=preview")));
    EXPECT_FALSE(policy.downloadFormatSelector.contains(QStringLiteral("bestaudio/best")));
    EXPECT_FALSE(policy.downloadFormatSelector.contains(QStringLiteral("mp3")));
    EXPECT_FALSE(policy.downloadFormatSelector.contains(QStringLiteral("opus")));

    EXPECT_EQ(QStringLiteral("scsearch25:artist track"),
            ReleasesService::searchInput(
                    ReleaseProvider::SoundCloud, QStringLiteral("artist track"), true));
    EXPECT_EQ(QStringLiteral("ytsearch25:artist track music"),
            ReleasesService::searchInput(
                    ReleaseProvider::YouTube, QStringLiteral("artist track"), true));
}

TEST(ReleasesServiceTest, SoundCloudNeverUsesBrowserCookies) {
    EXPECT_TRUE(ReleasesService::browserCookieArguments(ReleaseProvider::SoundCloud,
            true,
            QStringLiteral("chrome"),
            QStringLiteral("Profile 1"))
                    .isEmpty());

    EXPECT_EQ(QStringList({QStringLiteral("--cookies-from-browser"),
                      QStringLiteral("chrome:Profile 1")}),
            ReleasesService::browserCookieArguments(ReleaseProvider::YouTube,
                    true,
                    QStringLiteral("chrome"),
                    QStringLiteral("Profile 1")));
}

TEST(ReleasesServiceTest, CalculatesProgress) {
    EXPECT_EQ(50, ReleasesService::progressPercent(50, 100));
    EXPECT_EQ(100, ReleasesService::progressPercent(200, 100));
    EXPECT_EQ(-1, ReleasesService::progressPercent(50, 0));
    EXPECT_EQ(-1, ReleasesService::progressPercent(-1, 100));
}

} // namespace mixxx::library::releases
