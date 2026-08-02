#pragma once

#include "library/libraryfeature.h"
#include "library/releases/releasesservice.h"
#include "util/parented_ptr.h"

class KeyboardEventFilter;
class TrackCollectionManager;

namespace mixxx::library::releasessc {

class SoundCloudFeature final : public LibraryFeature {
    Q_OBJECT
  public:
    SoundCloudFeature(Library* library,
            UserSettingsPointer config,
            TrackCollectionManager* trackCollectionManager);
    ~SoundCloudFeature() override = default;

    QVariant title() override;
    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;
    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;

  private slots:
    void slotLoadReady(TrackPointer track, const QString& group, bool play);

  private:
    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<mixxx::library::releases::ReleasesService> m_pService;
};

} // namespace mixxx::library::releasessc
