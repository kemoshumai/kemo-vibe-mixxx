#include "library/releasessc/soundcloudfeature.h"

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/releases/releasesprefs.h"
#include "library/releases/releasesview.h"
#include "library/treeitem.h"
#include "moc_soundcloudfeature.cpp"
#include "widget/wlibrary.h"

namespace mixxx::library::releasessc {

namespace {
const QString kViewName = QStringLiteral("Releases SC");
}

SoundCloudFeature::SoundCloudFeature(Library* library,
        UserSettingsPointer config,
        TrackCollectionManager* trackCollectionManager)
        : LibraryFeature(library, config, QStringLiteral("releases_sc")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pService(make_parented<mixxx::library::releases::ReleasesService>(this,
                  UserSettingsPointer(config),
                  trackCollectionManager,
                  mixxx::library::releases::ReleaseProvider::SoundCloud,
                  mixxx::library::releases::prefs::kSoundCloudDownloadDirectoryConfigKey,
                  QStringLiteral("releases-sc"))) {
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    connect(m_pService,
            &mixxx::library::releases::ReleasesService::loadReady,
            this,
            &SoundCloudFeature::slotLoadReady);
}

QVariant SoundCloudFeature::title() {
    return QVariant(tr("releases-sc"));
}

TreeItemModel* SoundCloudFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void SoundCloudFeature::bindLibraryWidget(WLibrary* libraryWidget,
        KeyboardEventFilter* keyboard) {
    auto* view = new mixxx::library::releases::ReleasesView(libraryWidget,
            UserSettingsPointer(m_pConfig),
            m_pService,
            keyboard);
    libraryWidget->registerView(kViewName, view);
}

void SoundCloudFeature::activate() {
    emit switchToView(kViewName);
    emit enableCoverArtDisplay(false);
}

void SoundCloudFeature::slotLoadReady(TrackPointer track,
        const QString& group,
        bool play) {
    if (!track) {
        return;
    }
    if (group.isEmpty()) {
        emit loadTrack(track);
        return;
    }
#ifdef __STEM__
    emit loadTrackToPlayer(track, group, mixxx::StemChannelSelection(), play);
#else
    emit loadTrackToPlayer(track, group, play);
#endif
}

} // namespace mixxx::library::releasessc
