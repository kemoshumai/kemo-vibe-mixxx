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

TEST(ReleasesServiceTest, CalculatesProgress) {
    EXPECT_EQ(50, ReleasesService::progressPercent(50, 100));
    EXPECT_EQ(100, ReleasesService::progressPercent(200, 100));
    EXPECT_EQ(-1, ReleasesService::progressPercent(50, 0));
    EXPECT_EQ(-1, ReleasesService::progressPercent(-1, 100));
}

} // namespace mixxx::library::releases
