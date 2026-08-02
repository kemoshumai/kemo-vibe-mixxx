#include "library/releases/releasesview.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkReply>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "mixer/playermanager.h"
#include "mixxxmainwindow.h"
#include "moc_releasesview.cpp"

namespace mixxx::library::releases {

namespace {

class ReleaseProgressDelegate final : public QStyledItemDelegate {
  public:
    explicit ReleaseProgressDelegate(QObject* parent)
            : QStyledItemDelegate(parent) {
    }

    void paint(QPainter* painter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override {
        const auto progress = index.data(Qt::UserRole).toInt();
        if (progress < 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionProgressBar progressOption;
        progressOption.rect = option.rect.adjusted(3, 5, -3, -5);
        progressOption.minimum = 0;
        progressOption.maximum = 100;
        progressOption.progress = progress;
        progressOption.text = QStringLiteral("%1 %").arg(progress);
        progressOption.textVisible = true;
        QApplication::style()->drawControl(QStyle::CE_ProgressBar, &progressOption, painter);
    }
};

QString durationText(qint64 seconds) {
    if (seconds <= 0) {
        return {};
    }
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

} // anonymous namespace

ReleasesTableModel::ReleasesTableModel(QObject* parent)
        : QAbstractTableModel(parent) {
}

void ReleasesTableModel::setResults(const QList<ReleaseSearchResult>& results) {
    beginResetModel();
    m_results = results;
    endResetModel();
}

void ReleasesTableModel::updateResult(const QString& key, int progress, const QString& status) {
    for (int row = 0; row < m_results.size(); ++row) {
        if (m_results.at(row).key != key) {
            continue;
        }
        m_results[row].progress = progress;
        m_results[row].status = status;
        emit dataChanged(index(row, Status), index(row, Progress));
        return;
    }
}

ReleaseSearchResult ReleasesTableModel::resultAt(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return {};
    }
    return m_results.at(index.row());
}

int ReleasesTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_results.size();
}

int ReleasesTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : Count;
}

QVariant ReleasesTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return {};
    }
    const auto& result = m_results.at(index.row());
    if (role == Qt::UserRole && index.column() == Progress) {
        return result.progress;
    }
    if (role == Qt::DecorationRole && index.column() == Thumbnail) {
        return m_thumbnails.value(result.key);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case Title:
        return result.title;
    case Uploader:
        return result.uploader;
    case Duration:
        return durationText(result.durationSeconds);
    case Status:
        return result.status.isEmpty() ? (result.cached ? tr("Saved") : QString()) : result.status;
    case Progress:
        return result.progress < 0 ? QString() : QStringLiteral("%1 %").arg(result.progress);
    default:
        return {};
    }
}

QVariant ReleasesTableModel::headerData(int section,
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
    case Uploader:
        return tr("Uploader");
    case Duration:
        return tr("Duration");
    case Status:
        return tr("Status");
    case Progress:
        return tr("Progress");
    default:
        return {};
    }
}

Qt::ItemFlags ReleasesTableModel::flags(const QModelIndex& index) const {
    return index.isValid() ? QAbstractTableModel::flags(index) : Qt::NoItemFlags;
}

void ReleasesTableModel::setThumbnail(const QString& key, const QImage& image) {
    if (image.isNull()) {
        return;
    }
    m_thumbnails.insert(key,
            QPixmap::fromImage(image.scaled(
                    96, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    for (int row = 0; row < m_results.size(); ++row) {
        if (m_results.at(row).key == key) {
            emit dataChanged(index(row, Thumbnail), index(row, Thumbnail));
            return;
        }
    }
}

ReleasesView::ReleasesView(QWidget* parent,
        UserSettingsPointer config,
        ReleasesService* service,
        KeyboardEventFilter* keyboard)
        : QWidget(parent),
          m_pConfig(std::move(config)),
          m_pService(service) {
    setObjectName(QStringLiteral("LibraryFeatureControls"));
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    m_pSearchEdit = new QLineEdit(this);
    m_pSearchEdit->setPlaceholderText(tr("Search"));
    m_pMusicOnly = new QCheckBox(tr("Music only"), this);
    m_pMusicOnly->setObjectName(QStringLiteral("ReleasesMusicOnlyCheckBox"));
    m_pMusicOnly->setChecked(true);
    m_pMusicOnly->setVisible(m_pService->provider() != ReleaseProvider::SoundCloud);
    m_pSearchButton = new QPushButton(tr("Search"), this);
    m_pSettingsButton = new QPushButton(tr("Settings"), this);
    controls->addWidget(m_pSearchEdit, 1);
    controls->addWidget(m_pMusicOnly);
    controls->addWidget(m_pSearchButton);
    controls->addWidget(m_pSettingsButton);
    layout->addLayout(controls);

    m_pStatusLabel = new QLabel(this);
    layout->addWidget(m_pStatusLabel);
    m_pTable = new QTableView(this);
    m_pModel = new ReleasesTableModel(m_pTable);
    m_pTable->setModel(m_pModel);
    m_pTable->setItemDelegateForColumn(ReleasesTableModel::Progress,
            new ReleaseProgressDelegate(m_pTable));
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setShowGrid(false);
    m_pTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pTable->verticalHeader()->hide();
    m_pTable->verticalHeader()->setDefaultSectionSize(58);
    auto* header = m_pTable->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ReleasesTableModel::Thumbnail, QHeaderView::Fixed);
    header->setSectionResizeMode(ReleasesTableModel::Title, QHeaderView::Stretch);
    header->setSectionResizeMode(ReleasesTableModel::Uploader, QHeaderView::Interactive);
    header->setSectionResizeMode(ReleasesTableModel::Duration, QHeaderView::Fixed);
    header->setSectionResizeMode(ReleasesTableModel::Status, QHeaderView::Fixed);
    header->setSectionResizeMode(ReleasesTableModel::Progress, QHeaderView::Fixed);
    m_pTable->setColumnWidth(ReleasesTableModel::Thumbnail, 108);
    m_pTable->setColumnWidth(ReleasesTableModel::Uploader, 220);
    m_pTable->setColumnWidth(ReleasesTableModel::Duration, 72);
    m_pTable->setColumnWidth(ReleasesTableModel::Status, 110);
    m_pTable->setColumnWidth(ReleasesTableModel::Progress, 120);
    layout->addWidget(m_pTable, 1);

    connect(m_pSearchButton, &QPushButton::clicked, this, &ReleasesView::slotSearch);
    connect(m_pSettingsButton, &QPushButton::clicked, this, [this] {
        auto* mainWindow = qobject_cast<MixxxMainWindow*>(window());
        if (mainWindow) {
            mainWindow->slotOptionsLibraryPreferences();
        }
    });
    connect(m_pSearchEdit, &QLineEdit::returnPressed, this, &ReleasesView::slotSearch);
    connect(m_pTable,
            &QTableView::doubleClicked,
            this,
            [this](const QModelIndex&) { requestLoad(); });
    connect(m_pTable,
            &QTableView::customContextMenuRequested,
            this,
            &ReleasesView::slotContextMenu);
    connect(m_pService,
            &ReleasesService::searchStarted,
            this,
            [this] { m_pStatusLabel->setText(tr("Searching")); });
    connect(m_pService,
            &ReleasesService::searchFinished,
            this,
            &ReleasesView::slotSearchFinished);
    connect(m_pService,
            &ReleasesService::resultUpdated,
            m_pModel,
            &ReleasesTableModel::updateResult);
    connect(m_pService,
            &ReleasesService::error,
            this,
            &ReleasesView::slotError);
    if (keyboard) {
        installEventFilter(keyboard);
        m_pTable->installEventFilter(keyboard);
    }
}

void ReleasesView::onShow() {
    QTimer::singleShot(0, this, &ReleasesView::applyThemePalette);
    m_controllerTableFocused = false;
    m_pSearchEdit->setFocus();
}

void ReleasesView::applyThemePalette() {
    QColor background = palette().color(QPalette::Window);
    const auto snapshot = grab(QRect(1, 1, 1, 1)).toImage();
    if (!snapshot.isNull()) {
        background = snapshot.pixelColor(0, 0);
    }
    const QColor foreground = background.lightness() < 128
            ? QColor(0xf0, 0xf0, 0xf0)
            : QColor(0x20, 0x20, 0x20);
    const auto blend = [](const QColor& first, const QColor& second, int secondWeight) {
        const int firstWeight = 100 - secondWeight;
        return QColor((first.red() * firstWeight + second.red() * secondWeight) / 100,
                (first.green() * firstWeight + second.green() * secondWeight) / 100,
                (first.blue() * firstWeight + second.blue() * secondWeight) / 100);
    };
    const auto alternateBackground = blend(background, foreground, 7);
    const auto headerBackground = blend(background, foreground, 11);
    const auto headerBorder = blend(background, foreground, 20);

    const QList<QWidget*> widgets{this,
            m_pSearchEdit,
            m_pMusicOnly,
            m_pSearchButton,
            m_pSettingsButton,
            m_pStatusLabel,
            m_pTable,
            m_pTable->horizontalHeader(),
            m_pTable->verticalHeader()};
    for (auto* widget : widgets) {
        auto widgetPalette = widget->palette();
        widgetPalette.setColor(QPalette::WindowText, foreground);
        widgetPalette.setColor(QPalette::Text, foreground);
        widgetPalette.setColor(QPalette::ButtonText, foreground);
        widgetPalette.setColor(QPalette::PlaceholderText, foreground);
        widget->setPalette(widgetPalette);
    }

    auto tablePalette = m_pTable->palette();
    tablePalette.setColor(QPalette::Base, background);
    tablePalette.setColor(QPalette::AlternateBase, alternateBackground);
    tablePalette.setColor(QPalette::Window, background);
    tablePalette.setColor(QPalette::Text, foreground);
    tablePalette.setColor(QPalette::WindowText, foreground);
    m_pTable->setPalette(tablePalette);
    m_pTable->viewport()->setPalette(tablePalette);
    m_pTable->viewport()->setAutoFillBackground(true);

    auto headerPalette = m_pTable->horizontalHeader()->palette();
    headerPalette.setColor(QPalette::Button, headerBackground);
    headerPalette.setColor(QPalette::Window, headerBackground);
    headerPalette.setColor(QPalette::ButtonText, foreground);
    headerPalette.setColor(QPalette::WindowText, foreground);
    m_pTable->horizontalHeader()->setPalette(headerPalette);
    m_pTable->horizontalHeader()->setStyleSheet(QStringLiteral(
            "QHeaderView::section {"
            "background-color: %1;"
            "color: %2;"
            "border: 0;"
            "border-right: 1px solid %3;"
            "border-bottom: 1px solid %3;"
            "padding: 4px;"
            "}")
                    .arg(headerBackground.name(),
                            foreground.name(),
                            headerBorder.name()));
}

bool ReleasesView::hasFocus() const {
    return m_pSearchEdit->hasFocus() || m_pTable->hasFocus();
}

void ReleasesView::setFocus() {
    m_controllerTableFocused = true;
    focusTableForController();
    if (!m_pSearchEdit->hasFocus()) {
        m_pTable->setFocus(Qt::OtherFocusReason);
    }
}

bool ReleasesView::loadSelectedTrackToGroup(const QString& group, bool play) {
    return requestLoad(group, play);
}

bool ReleasesView::handleLibraryKeyEvent(QKeyEvent* event) {
    if (!event || !m_pSearchEdit->hasFocus()) {
        return false;
    }

    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        setFocus();
        return true;
    default:
        break;
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

void ReleasesView::focusTableForController() {
    if (m_pModel->rowCount() == 0) {
        selectFirstResult();
        return;
    }

    auto currentIndex = m_pTable->currentIndex();
    if (!currentIndex.isValid() || currentIndex.row() >= m_pModel->rowCount()) {
        currentIndex = m_pModel->index(0, ReleasesTableModel::Title);
    }
    m_pTable->setCurrentIndex(currentIndex);
    m_pTable->selectRow(currentIndex.row());
    m_pTable->scrollTo(currentIndex);
}

void ReleasesView::selectFirstResult() {
    if (m_pModel->rowCount() == 0) {
        m_pTable->clearSelection();
        m_pTable->setCurrentIndex(QModelIndex());
        return;
    }

    const auto firstIndex = m_pModel->index(0, ReleasesTableModel::Title);
    m_pTable->setCurrentIndex(firstIndex);
    m_pTable->selectRow(0);
    m_pTable->scrollTo(firstIndex);
}

void ReleasesView::onSearch(const QString& text) {
    m_pSearchEdit->setText(text);
    if (!text.isEmpty()) {
        slotSearch();
    }
}

void ReleasesView::slotSearch() {
    const auto query = m_pSearchEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    m_pService->search(query, m_pMusicOnly->isChecked());
}

void ReleasesView::slotSearchFinished(const QList<ReleaseSearchResult>& results) {
    auto hydratedResults = results;
    for (auto& result : hydratedResults) {
        result.cached = m_pService->isCached(result.key);
    }
    m_pModel->setResults(hydratedResults);
    selectFirstResult();
    m_pStatusLabel->setText(tr("%1 results").arg(hydratedResults.size()));
    fetchThumbnails(hydratedResults);
}

void ReleasesView::slotError(const QString& message) {
    m_pStatusLabel->setText(message);
}

void ReleasesView::fetchThumbnails(const QList<ReleaseSearchResult>& results) {
    for (const auto& result : results) {
        if (result.thumbnailUrl.isEmpty()) {
            continue;
        }
        const auto cachedPath = m_pService->thumbnailPath(result.key);
        QImage cached(cachedPath);
        if (!cached.isNull()) {
            m_pModel->setThumbnail(result.key, cached);
            continue;
        }
        QNetworkRequest request(QUrl(result.thumbnailUrl));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setRawHeader(QByteArrayLiteral("User-Agent"),
                QByteArrayLiteral("Mozilla/5.0"));
        auto* reply = m_network.get(request);
        m_thumbnailRequests.insert(reply, result.key);
        connect(reply, &QNetworkReply::finished, this, &ReleasesView::slotThumbnailFinished);
    }
}

void ReleasesView::slotThumbnailFinished() {
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

bool ReleasesView::requestLoad(const QString& group, bool play) {
    const auto index = m_pTable->currentIndex();
    const auto result = m_pModel->resultAt(index);
    if (result.key.isEmpty()) {
        return false;
    }
    m_pService->requestLoad(result, group, play);
    return true;
}

void ReleasesView::slotContextMenu(const QPoint& position) {
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

} // namespace mixxx::library::releases
