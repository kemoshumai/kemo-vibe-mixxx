#include <gtest/gtest.h>

#include <QKeyEvent>
#include <QLineEdit>
#include <QTableView>
#include <QTest>
#include <QWidget>

#include "library/releases/releasesview.h"
#include "test/mixxxtest.h"

namespace mixxx::library::releases {

namespace {

QList<ReleaseSearchResult> makeResults(int count) {
    QList<ReleaseSearchResult> results;
    for (int i = 0; i < count; ++i) {
        ReleaseSearchResult result;
        result.key = QStringLiteral("result-%1").arg(i);
        result.title = QStringLiteral("Result %1").arg(i);
        results.append(result);
    }
    return results;
}

class ReleasesViewTest : public MixxxTest {
  protected:
    struct TestView {
        explicit TestView(UserSettingsPointer config)
                : window(),
                  config(std::move(config)),
                  service(&window, this->config, nullptr),
                  view(&window, this->config, &service, nullptr) {
            window.show();
            view.show();
        }

        QWidget window;
        UserSettingsPointer config;
        ReleasesService service;
        ReleasesView view;
    };
};

} // namespace

TEST_F(ReleasesViewTest, SearchResultsResetSelectionToFirstRow) {
    TestView testView(config());
    auto* table = testView.view.findChild<QTableView*>();
    ASSERT_NE(nullptr, table);

    emit testView.service.searchFinished(makeResults(3));
    ASSERT_EQ(0, table->currentIndex().row());

    table->selectRow(2);
    ASSERT_EQ(2, table->currentIndex().row());

    emit testView.service.searchFinished(makeResults(2));
    EXPECT_EQ(0, table->currentIndex().row());
    ASSERT_EQ(1, table->selectionModel()->selectedRows().size());
    EXPECT_EQ(0, table->selectionModel()->selectedRows().first().row());

    emit testView.service.searchFinished({});
    EXPECT_FALSE(table->currentIndex().isValid());
    EXPECT_TRUE(table->selectionModel()->selectedRows().isEmpty());
}

TEST_F(ReleasesViewTest, ControllerNavigationKeepsSearchFocus) {
    TestView testView(config());
    auto* searchEdit = testView.view.findChild<QLineEdit*>();
    auto* table = testView.view.findChild<QTableView*>();
    ASSERT_NE(nullptr, searchEdit);
    ASSERT_NE(nullptr, table);

    emit testView.service.searchFinished(makeResults(3));
    testView.window.activateWindow();
    QApplication::processEvents();
    searchEdit->setFocus();
    QApplication::processEvents();
    ASSERT_TRUE(searchEdit->hasFocus());

    QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    EXPECT_TRUE(testView.view.handleLibraryKeyEvent(&tabEvent));
    EXPECT_TRUE(searchEdit->hasFocus());

    QKeyEvent downEvent(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    EXPECT_TRUE(testView.view.handleLibraryKeyEvent(&downEvent));
    EXPECT_EQ(1, table->currentIndex().row());
    EXPECT_TRUE(searchEdit->hasFocus());

    QTest::keyClicks(searchEdit, QStringLiteral("query"));
    EXPECT_EQ(QStringLiteral("query"), searchEdit->text());
}

} // namespace mixxx::library::releases
