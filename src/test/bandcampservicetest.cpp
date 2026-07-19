#include <gtest/gtest.h>

#include "library/releasescamp/bandcampservice.h"

namespace mixxx::library::releasescamp {

TEST(BandcampServiceTest, ParsesTrackSearchResultsOnly) {
    const QByteArray json = R"JSON({
        "auto": {"results": [
            {"type":"t","id":123,"name":"Free song","band_name":"Artist",
             "album_name":"Album","item_url_path":"https://artist.bandcamp.com/track/free-song",
             "img":"https://f4.bcbits.com/img/123_3.jpg"},
            {"type":"a","id":456,"name":"Album","item_url_path":"https://artist.bandcamp.com/album/album"},
            {"type":"t","id":123,"name":"Duplicate","item_url_path":"https://artist.bandcamp.com/track/free-song"}
        ]}
    })JSON";

    const auto results = BandcampService::parseSearchJson(json);
    ASSERT_EQ(1, results.size());
    EXPECT_EQ(QStringLiteral("123"), results.at(0).id);
    EXPECT_EQ(QStringLiteral("Free song"), results.at(0).title);
    EXPECT_EQ(QStringLiteral("Artist"), results.at(0).artist);
}

TEST(BandcampServiceTest, ClassifiesFreeAndNameYourPriceZero) {
    BandcampSearchCandidate candidate;
    candidate.id = QStringLiteral("123");
    candidate.title = QStringLiteral("Song");
    candidate.url = QStringLiteral("https://artist.bandcamp.com/track/song");

    const QByteArray freePage =
            R"HTML(<script data-tralbum="{&quot;current&quot;:{&quot;download_pref&quot;:1},&quot;trackinfo&quot;:[{&quot;duration&quot;:120,&quot;has_free_download&quot;:true}]}" ></script>)HTML";
    const auto free = BandcampService::parseTrackPage(freePage, candidate);
    ASSERT_TRUE(free.has_value());
    EXPECT_EQ(BandcampPriceKind::Free, free->priceKind);

    const QByteArray nypPage =
            R"HTML(<script data-tralbum="{&quot;current&quot;:{&quot;download_pref&quot;:2,&quot;minimum_price&quot;:0},&quot;freeDownloadPage&quot;:&quot;https://bandcamp.com/download?id=1&quot;,&quot;trackinfo&quot;:[{&quot;duration&quot;:121}]}" ></script>)HTML";
    const auto nyp = BandcampService::parseTrackPage(nypPage, candidate);
    ASSERT_TRUE(nyp.has_value());
    EXPECT_EQ(BandcampPriceKind::NameYourPriceZero, nyp->priceKind);

    const QByteArray paidPage =
            R"HTML(<script data-tralbum="{&quot;current&quot;:{&quot;download_pref&quot;:2,&quot;minimum_price&quot;:1},&quot;trackinfo&quot;:[{&quot;duration&quot;:121}]}" ></script>)HTML";
    EXPECT_FALSE(BandcampService::parseTrackPage(paidPage, candidate).has_value());
}

TEST(BandcampServiceTest, AppliesPriceFilters) {
    EXPECT_TRUE(BandcampService::matchesFilter(
            BandcampPriceKind::Free, BandcampPriceFilter::Both));
    EXPECT_TRUE(BandcampService::matchesFilter(
            BandcampPriceKind::NameYourPriceZero, BandcampPriceFilter::NameYourPriceZeroOnly));
    EXPECT_FALSE(BandcampService::matchesFilter(
            BandcampPriceKind::Free, BandcampPriceFilter::NameYourPriceZeroOnly));
}

TEST(BandcampServiceTest, ParsesExactAlbumTracksWithAlbumPrice) {
    BandcampAlbumCandidate album;
    album.title = QStringLiteral("FASTFUSION");
    album.url = QStringLiteral("https://lostfrog.bandcamp.com/album/fastfusion");
    album.thumbnailUrl = QStringLiteral("https://f4.bcbits.com/img/1_3.jpg");
    const QByteArray page =
            R"HTML(<script data-tralbum="{&quot;current&quot;:{&quot;title&quot;:&quot;FASTFUSION&quot;,&quot;download_pref&quot;:2,&quot;minimum_price&quot;:0},&quot;freeDownloadPage&quot;:&quot;https://bandcamp.com/download?id=1&quot;,&quot;trackinfo&quot;:[{&quot;track_id&quot;:2517181670,&quot;title_link&quot;:&quot;/track/do-you-understand-how-i-feel&quot;,&quot;title&quot;:&quot;samebeam - do you understand how i feel?&quot;,&quot;artist&quot;:&quot;samebeam&quot;,&quot;duration&quot;:170.128}]}"></script>)HTML";

    const auto results = BandcampService::parseAlbumPage(page, album);
    ASSERT_EQ(1, results.size());
    EXPECT_EQ(QStringLiteral("bandcamp-2517181670"), results.at(0).release.key);
    EXPECT_EQ(QStringLiteral("https://lostfrog.bandcamp.com/track/do-you-understand-how-i-feel"),
            results.at(0).release.webpageUrl);
    EXPECT_EQ(BandcampPriceKind::NameYourPriceZero, results.at(0).priceKind);
}

} // namespace mixxx::library::releasescamp
