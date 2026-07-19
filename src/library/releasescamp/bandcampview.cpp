#include "library/releasescamp/bandcampview.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/releases/releasesservice.h"
#include "mixer/playermanager.h"
#include "mixxxmainwindow.h"
#include "moc_bandcampview.cpp"

namespace mixxx::library::releasescamp {

namespace {

QString durationText(qint64 seconds) {
    if (seconds <= 0) {
        return {};
    }
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

QString priceText(BandcampPriceKind kind) {
    return kind == BandcampPriceKind::Free ? QObject::tr("Free") : QObject::tr("NYP 0");
}

} // namespace

BandcampTableModel::BandcampTableModel(QObject* parent)
        : QAbstractTableModel(parent) {
}

void BandcampTableModel::setResults(const QList<BandcampSearchResult>& results) {
    beginResetModel();
    m_results = results;
    endResetModel();
}

void BandcampTableModel::updateResult(const QString& key, int progress, const QString& status) {
    for (int row = 0; row < m_results.size(); ++row) {
        if (m_results.at(row).release.key != key) {
            continue;
        }
        m_results[row].release.progress = progress;
        m_results[row].release.status = status;
        emit dataChanged(index(row, Status), index(row, Progress));
        return;
    }
}

BandcampSearchResult BandcampTableModel::resultAt(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return {};
    }
    return m_results.at(index.row());
}

int BandcampTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_results.size();
}

int BandcampTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : Count;
}

QVariant BandcampTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return {};
    }
    const auto& result = m_results.at(index.row());
    if (role == Qt::UserRole && index.column() == Progress) {
        return result.release.progress;
    }
    if (role == Qt::DecorationRole && index.column() == Thumbnail) {
        return m_thumbnails.value(result.release.key);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case Title:
        return result.release.title;
    case Artist:
        return result.release.uploader;
    case Album:
        return result.release.album;
    case Duration:
        return durationText(result.release.durationSeconds);
    case Price:
        return priceText(result.priceKind);
    case Status:
        return result.release.status.isEmpty()
                ? (result.release.cached ? tr("Saved") : QString())
                : result.release.status;
    case Progress:
        return result.release.progress < 0
                ? QString()
                : QStringLiteral("%1 %").arg(result.release.progress);
    default:
        return {};
    }
}

QVariant BandcampTableModel::headerData(int section,
        Qt::Orientation orientation,
        int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case Thumbnail:
        return tr("Thumbnail");
    case Title:
        return tr("Title");
    case Artist:
        return tr("Artist");
    case Album:
        return tr("Album");
    case Duration:
        return tr("Duration");
    case Price:
        return tr("Price");
    case Status:
        return tr("Status");
    case Progress:
        return tr("Progress");
    default:
        return {};
    }
}

Qt::ItemFlags BandcampTableModel::flags(const QModelIndex& index) const {
    return index.isValid() ? QAbstractTableModel::flags(index) : Qt::NoItemFlags;
}

void BandcampTableModel::setThumbnail(const QString& key, const QImage& image) {
    if (image.isNull()) {
        return;
    }
    m_thumbnails.insert(key,
            QPixmap::fromImage(image.scaled(96, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    for (int row = 0; row < m_results.size(); ++row) {
        if (m_results.at(row).release.key == key) {
            emit dataChanged(index(row, Thumbnail), index(row, Thumbnail));
            return;
        }
    }
}

BandcampView::BandcampView(QWidget* parent,
        UserSettingsPointer config,
        BandcampService* service,
        KeyboardEventFilter* keyboard)
        : QWidget(parent),
          m_pConfig(std::move(config)),
          m_pService(service) {
    setObjectName(QStringLiteral("LibraryFeatureControls"));
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    m_pSearchEdit = new QLineEdit(this);
    m_pSearchEdit->setPlaceholderText(tr("Search"));
    m_pPriceFilter = new QComboBox(this);
    m_pPriceFilter->addItem(tr("Free and NYP 0"),
            static_cast<int>(BandcampPriceFilter::Both));
    m_pPriceFilter->addItem(tr("Free only"),
            static_cast<int>(BandcampPriceFilter::FreeOnly));
    m_pPriceFilter->addItem(tr("NYP 0 only"),
            static_cast<int>(BandcampPriceFilter::NameYourPriceZeroOnly));
    m_pSearchButton = new QPushButton(tr("Search"), this);
    m_pSettingsButton = new QPushButton(tr("Settings"), this);
    controls->addWidget(m_pSearchEdit, 1);
    controls->addWidget(m_pPriceFilter);
    controls->addWidget(m_pSearchButton);
    controls->addWidget(m_pSettingsButton);
    layout->addLayout(controls);
    m_pStatusLabel = new QLabel(this);
    layout->addWidget(m_pStatusLabel);
    m_pTable = new QTableView(this);
    m_pModel = new BandcampTableModel(m_pTable);
    m_pTable->setModel(m_pModel);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setShowGrid(false);
    m_pTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pTable->verticalHeader()->hide();
    m_pTable->verticalHeader()->setDefaultSectionSize(58);
    auto* header = m_pTable->horizontalHeader();
    header->setSectionResizeMode(BandcampTableModel::Thumbnail, QHeaderView::Fixed);
    header->setSectionResizeMode(BandcampTableModel::Title, QHeaderView::Stretch);
    header->setSectionResizeMode(BandcampTableModel::Artist, QHeaderView::Interactive);
    header->setSectionResizeMode(BandcampTableModel::Album, QHeaderView::Interactive);
    header->setSectionResizeMode(BandcampTableModel::Duration, QHeaderView::Fixed);
    header->setSectionResizeMode(BandcampTableModel::Price, QHeaderView::Fixed);
    header->setSectionResizeMode(BandcampTableModel::Status, QHeaderView::Fixed);
    header->setSectionResizeMode(BandcampTableModel::Progress, QHeaderView::Fixed);
    m_pTable->setColumnWidth(BandcampTableModel::Thumbnail, 108);
    m_pTable->setColumnWidth(BandcampTableModel::Artist, 180);
    m_pTable->setColumnWidth(BandcampTableModel::Album, 180);
    m_pTable->setColumnWidth(BandcampTableModel::Duration, 72);
    m_pTable->setColumnWidth(BandcampTableModel::Price, 90);
    m_pTable->setColumnWidth(BandcampTableModel::Status, 110);
    m_pTable->setColumnWidth(BandcampTableModel::Progress, 90);
    layout->addWidget(m_pTable, 1);

    connect(m_pSearchButton, &QPushButton::clicked, this, &BandcampView::slotSearch);
    connect(m_pSearchEdit, &QLineEdit::returnPressed, this, &BandcampView::slotSearch);
    connect(m_pSettingsButton, &QPushButton::clicked, this, [this] {
        if (auto* mainWindow = qobject_cast<MixxxMainWindow*>(window())) {
            mainWindow->slotOptionsLibraryPreferences();
        }
    });
    connect(m_pTable, &QTableView::doubleClicked, this, [this](const QModelIndex&) { requestLoad(); });
    connect(m_pTable, &QTableView::customContextMenuRequested, this, &BandcampView::slotContextMenu);
    connect(m_pService, &BandcampService::searchStarted, this, [this] { m_pStatusLabel->setText(tr("Searching")); });
    connect(m_pService, &BandcampService::searchFinished, this, &BandcampView::slotSearchFinished);
    connect(m_pService, &BandcampService::resultUpdated, m_pModel, &BandcampTableModel::updateResult);
    connect(m_pService, &BandcampService::error, this, &BandcampView::slotError);
    if (keyboard) {
        installEventFilter(keyboard);
        m_pTable->installEventFilter(keyboard);
    }
}

void BandcampView::onShow() {
    m_controllerTableFocused = false;
    m_pSearchEdit->setFocus();
}

bool BandcampView::hasFocus() const {
    return m_pSearchEdit->hasFocus() || m_pTable->hasFocus();
}

void BandcampView::setFocus() {
    m_controllerTableFocused = true;
    focusTableForController();
    if (!m_pSearchEdit->hasFocus()) {
        m_pTable->setFocus(Qt::OtherFocusReason);
    }
}

bool BandcampView::loadSelectedTrackToGroup(const QString& group, bool play) {
    return requestLoad(group, play);
}

bool BandcampView::handleLibraryKeyEvent(QKeyEvent* event) {
    if (!event || !m_pSearchEdit->hasFocus()) {
        return false;
    }
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        setFocus();
        return true;
    }
    if (!m_controllerTableFocused) {
        return false;
    }
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Home:
    case Qt::Key_End:
        focusTableForController();
        QApplication::sendEvent(m_pTable, event);
        return true;
    default:
        return false;
    }
}

void BandcampView::focusTableForController() {
    if (m_pModel->rowCount() == 0) {
        selectFirstResult();
        return;
    }
    auto current = m_pTable->currentIndex();
    if (!current.isValid() || current.row() >= m_pModel->rowCount()) {
        current = m_pModel->index(0, BandcampTableModel::Title);
    }
    m_pTable->setCurrentIndex(current);
    m_pTable->selectRow(current.row());
    m_pTable->scrollTo(current);
}

void BandcampView::selectFirstResult() {
    if (m_pModel->rowCount() == 0) {
        m_pTable->clearSelection();
        m_pTable->setCurrentIndex(QModelIndex());
        return;
    }
    const auto first = m_pModel->index(0, BandcampTableModel::Title);
    m_pTable->setCurrentIndex(first);
    m_pTable->selectRow(0);
    m_pTable->scrollTo(first);
}

void BandcampView::onSearch(const QString& text) {
    m_pSearchEdit->setText(text);
    if (!text.isEmpty()) {
        slotSearch();
    }
}

void BandcampView::slotSearch() {
    const auto query = m_pSearchEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    m_pService->search(query,
            static_cast<BandcampPriceFilter>(m_pPriceFilter->currentData().toInt()));
}

void BandcampView::slotSearchFinished(const QList<BandcampSearchResult>& results) {
    auto hydrated = results;
    for (auto& result : hydrated) {
        result.release.cached = m_pService->isCached(result.release.key);
    }
    m_pModel->setResults(hydrated);
    selectFirstResult();
    m_pStatusLabel->setText(tr("%1 results").arg(hydrated.size()));
    fetchThumbnails(hydrated);
}

void BandcampView::slotError(const QString& message) {
    m_pStatusLabel->setText(message);
}

void BandcampView::fetchThumbnails(const QList<BandcampSearchResult>& results) {
    for (const auto& result : results) {
        if (result.release.thumbnailUrl.isEmpty()) {
            continue;
        }
        const auto cachedPath = m_pService->thumbnailPath(result.release.key);
        const QImage cached(cachedPath);
        if (!cached.isNull()) {
            m_pModel->setThumbnail(result.release.key, cached);
            continue;
        }
        QNetworkRequest request(QUrl(result.release.thumbnailUrl));
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
        auto* reply = m_network.get(request);
        m_thumbnailRequests.insert(reply, result.release.key);
        connect(reply, &QNetworkReply::finished, this, &BandcampView::slotThumbnailFinished);
    }
}

void BandcampView::slotThumbnailFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    const auto key = m_thumbnailRequests.take(reply);
    if (reply->error() == QNetworkReply::NoError) {
        const auto image = QImage::fromData(reply->readAll());
        if (!image.isNull()) {
            m_pService->cacheThumbnail(key, image);
            m_pModel->setThumbnail(key, image);
        }
    }
    reply->deleteLater();
}

bool BandcampView::requestLoad(const QString& group, bool play) {
    const auto result = m_pModel->resultAt(m_pTable->currentIndex());
    if (result.release.key.isEmpty()) {
        return false;
    }
    m_pService->requestLoad(result, group, play);
    return true;
}

void BandcampView::slotContextMenu(const QPoint& position) {
    const auto index = m_pTable->indexAt(position);
    if (!index.isValid()) {
        return;
    }
    m_pTable->setCurrentIndex(index);
    QMenu menu(this);
    menu.addAction(tr("Load to next available deck"), this, [this] { requestLoad(); });
    auto* deckMenu = menu.addMenu(tr("Load to deck"));
    const int decks = m_pConfig->getValue(ConfigKey("[App]", "num_decks"), 2);
    for (int i = 0; i < decks; ++i) {
        const auto group = PlayerManager::groupForDeck(i);
        deckMenu->addAction(tr("Deck %1").arg(i + 1), this, [this, group] { requestLoad(group); });
    }
    menu.exec(m_pTable->viewport()->mapToGlobal(position));
}

} // namespace mixxx::library::releasescamp
