#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPointer>
#include <QTableView>
#include <QWidget>

#include "library/libraryview.h"
#include "library/releases/releasesservice.h"

class KeyboardEventFilter;
class QCheckBox;
class QLabel;
class QLineEdit;
class QNetworkReply;
class QPushButton;

namespace mixxx::library::releases {

class ReleasesTableModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Column { Thumbnail,
        Title,
        Uploader,
        Duration,
        Status,
        Progress,
        Count };

    explicit ReleasesTableModel(QObject* parent = nullptr);

    void setResults(const QList<ReleaseSearchResult>& results);
    void updateResult(const QString& key, int progress, const QString& status);
    ReleaseSearchResult resultAt(const QModelIndex& index) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
            Qt::Orientation orientation,
            int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

  public slots:
    void setThumbnail(const QString& key, const QImage& image);

  private:
    QList<ReleaseSearchResult> m_results;
    QHash<QString, QPixmap> m_thumbnails;
};

class ReleasesView final : public QWidget, public LibraryView {
    Q_OBJECT
  public:
    ReleasesView(QWidget* parent,
            UserSettingsPointer config,
            ReleasesService* service,
            KeyboardEventFilter* keyboard);
    ~ReleasesView() override = default;

    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    bool loadSelectedTrackToGroup(const QString& group, bool play) override;
    bool handleLibraryKeyEvent(QKeyEvent* event) override;
    void onSearch(const QString& text) override;

  private slots:
    void slotSearch();
    void slotSearchFinished(const QList<ReleaseSearchResult>& results);
    void slotError(const QString& message);
    void slotContextMenu(const QPoint& position);
    void slotThumbnailFinished();

  private:
    void applyThemePalette();
    void focusTableForController();
    void selectFirstResult();
    bool requestLoad(const QString& group = QString(), bool play = false);
    void fetchThumbnails(const QList<ReleaseSearchResult>& results);

    UserSettingsPointer m_pConfig;
    ReleasesService* const m_pService;
    QLineEdit* m_pSearchEdit;
    QCheckBox* m_pMusicOnly;
    QPushButton* m_pSearchButton;
    QPushButton* m_pSettingsButton;
    QLabel* m_pStatusLabel;
    QTableView* m_pTable;
    ReleasesTableModel* m_pModel;
    bool m_controllerTableFocused{false};
    QNetworkAccessManager m_network;
    QHash<QNetworkReply*, QString> m_thumbnailRequests;
    QHash<QString, QImage> m_thumbnailImages;
};

} // namespace mixxx::library::releases
