#include "library/releasescamp/bandcampfeature.h"

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/releasescamp/bandcampview.h"
#include "library/treeitem.h"
#include "moc_bandcampfeature.cpp"
#include "widget/wlibrary.h"

namespace mixxx::library::releasescamp {

namespace {
const QString kViewName = QStringLiteral("Releases Camp");
}

BandcampFeature::BandcampFeature(Library* library,
        UserSettingsPointer config,
        TrackCollectionManager* trackCollectionManager)
        : LibraryFeature(library, config, QStringLiteral("releases_camp")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pService(make_parented<BandcampService>(this,
                  UserSettingsPointer(config),
                  trackCollectionManager)) {
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    connect(m_pService,
            &BandcampService::loadReady,
            this,
            &BandcampFeature::slotLoadReady);
}

QVariant BandcampFeature::title() {
    return QVariant(tr("releases-camp"));
}

TreeItemModel* BandcampFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void BandcampFeature::bindLibraryWidget(WLibrary* libraryWidget,
        KeyboardEventFilter* keyboard) {
    auto* view = new BandcampView(libraryWidget,
            UserSettingsPointer(m_pConfig),
            m_pService,
            keyboard);
    libraryWidget->registerView(kViewName, view);
}

void BandcampFeature::activate() {
    emit switchToView(kViewName);
    emit enableCoverArtDisplay(false);
}

void BandcampFeature::slotLoadReady(TrackPointer track,
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

} // namespace mixxx::library::releasescamp
