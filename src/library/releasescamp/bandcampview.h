#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QTableView>
#include <QWidget>

#include "library/libraryview.h"
#include "library/releasescamp/bandcampservice.h"

class KeyboardEventFilter;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkReply;
class QPushButton;

namespace mixxx::library::releasescamp {

class BandcampTableModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Column { Thumbnail,
        Title,
        Artist,
        Album,
        Duration,
        Price,
        Status,
        Progress,
        Count };

    explicit BandcampTableModel(QObject* parent = nullptr);
    void setResults(const QList<BandcampSearchResult>& results);
    void updateResult(const QString& key, int progress, const QString& status);
    BandcampSearchResult resultAt(const QModelIndex& index) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

  public slots:
    void setThumbnail(const QString& key, const QImage& image);

  private:
    QList<BandcampSearchResult> m_results;
    QHash<QString, QPixmap> m_thumbnails;
};

class BandcampView final : public QWidget, public LibraryView {
    Q_OBJECT
  public:
    BandcampView(QWidget* parent,
            UserSettingsPointer config,
            BandcampService* service,
            KeyboardEventFilter* keyboard);
    ~BandcampView() override = default;

    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    bool loadSelectedTrackToGroup(const QString& group, bool play) override;
    bool handleLibraryKeyEvent(QKeyEvent* event) override;
    void onSearch(const QString& text) override;

  private slots:
    void slotSearch();
    void slotSearchFinished(const QList<BandcampSearchResult>& results);
    void slotError(const QString& message);
    void slotContextMenu(const QPoint& position);
    void slotThumbnailFinished();

  private:
    void selectFirstResult();
    void focusTableForController();
    bool requestLoad(const QString& group = QString(), bool play = false);
    void fetchThumbnails(const QList<BandcampSearchResult>& results);

    UserSettingsPointer m_pConfig;
    BandcampService* const m_pService;
    QLineEdit* m_pSearchEdit;
    QComboBox* m_pPriceFilter;
    QPushButton* m_pSearchButton;
    QPushButton* m_pSettingsButton;
    QLabel* m_pStatusLabel;
    QTableView* m_pTable;
    BandcampTableModel* m_pModel;
    bool m_controllerTableFocused{false};
    QNetworkAccessManager m_network;
    QHash<QNetworkReply*, QString> m_thumbnailRequests;
};

} // namespace mixxx::library::releasescamp
