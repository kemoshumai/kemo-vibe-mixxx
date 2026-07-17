#include "library/releases/releasesfeature.h"

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/releases/releasesview.h"
#include "library/treeitem.h"
#include "moc_releasesfeature.cpp"
#include "widget/wlibrary.h"

namespace mixxx::library::releases {

namespace {
const QString kViewName = QStringLiteral("Releases");
}

ReleasesFeature::ReleasesFeature(Library* library,
        UserSettingsPointer config,
        TrackCollectionManager* trackCollectionManager)
        : LibraryFeature(library, config, QStringLiteral("releases")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pService(make_parented<ReleasesService>(this,
                  UserSettingsPointer(config),
                  trackCollectionManager)) {
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    connect(m_pService,
            &ReleasesService::loadReady,
            this,
            &ReleasesFeature::slotLoadReady);
}

QVariant ReleasesFeature::title() {
    return QVariant(tr("releases"));
}

TreeItemModel* ReleasesFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void ReleasesFeature::bindLibraryWidget(WLibrary* libraryWidget,
        KeyboardEventFilter* keyboard) {
    auto* view = new ReleasesView(libraryWidget,
            UserSettingsPointer(m_pConfig),
            m_pService,
            keyboard);
    libraryWidget->registerView(kViewName, view);
}

void ReleasesFeature::activate() {
    emit switchToView(kViewName);
    emit enableCoverArtDisplay(false);
}

void ReleasesFeature::slotLoadReady(TrackPointer track,
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

} // namespace mixxx::library::releases
