#include "library/releases/releasesprefs.h"

namespace mixxx::library::releases::prefs {

const ConfigKey kDownloadDirectoryConfigKey{
        QStringLiteral("[Releases]"), QStringLiteral("DownloadDirectory")};
const ConfigKey kBandcampDownloadDirectoryConfigKey{
        QStringLiteral("[Releases]"), QStringLiteral("BandcampDownloadDirectory")};
const ConfigKey kSoundCloudDownloadDirectoryConfigKey{
        QStringLiteral("[Releases]"), QStringLiteral("SoundCloudDownloadDirectory")};
const ConfigKey kHelperPathConfigKey{QStringLiteral("[Releases]"), QStringLiteral("HelperPath")};
const ConfigKey kUseBrowserCookiesConfigKey{
        QStringLiteral("[Releases]"), QStringLiteral("UseBrowserCookies")};
const ConfigKey kCookieBrowserConfigKey{
        QStringLiteral("[Releases]"), QStringLiteral("CookieBrowser")};
const ConfigKey kCookieProfileConfigKey{
        QStringLiteral("[Releases]"), QStringLiteral("CookieProfile")};

} // namespace mixxx::library::releases::prefs
