#pragma once

#include "library/libraryfeature.h"
#include "library/releasescamp/bandcampservice.h"
#include "util/parented_ptr.h"

class KeyboardEventFilter;
class TrackCollectionManager;

namespace mixxx::library::releasescamp {

class BandcampView;

class BandcampFeature final : public LibraryFeature {
    Q_OBJECT
  public:
    BandcampFeature(Library* library,
            UserSettingsPointer config,
            TrackCollectionManager* trackCollectionManager);
    ~BandcampFeature() override = default;

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
    parented_ptr<BandcampService> m_pService;
};

} // namespace mixxx::library::releasescamp
