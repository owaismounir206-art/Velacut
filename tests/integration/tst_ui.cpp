// SPDX-License-Identifier: GPL-3.0-or-later
// The real interface driven with the mouse and the keyboard, headless (software scene graph): the usability tests of
// SPEC §0bis counted in actions (clicks, keys, drags), and the timeline gestures. Set VEDIT_UI_SHOTS=<folder> to also
// save screenshots of each step (visual review).
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "theme/SystemAppearance.h"
#include "theme/ThemeManager.h"
#include "ui/controllers/AppController.h"
#include "ui/models/TimelineModel.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Vedit_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Vedit_StylePlugin)
Q_IMPORT_QML_PLUGIN(Vedit_ComponentsPlugin)
Q_IMPORT_QML_PLUGIN(Vedit_UIPlugin)

using namespace vedit;
using namespace vedit::test;
using namespace Qt::StringLiterals;

class TestUi : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;
    std::unique_ptr<theme::SystemAppearance> m_appearance;
    std::unique_ptr<theme::ThemeManager> m_theme;
    std::unique_ptr<ui::AppController> m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    int m_actions = 0;
    QStringList m_warnings;

    // ---- helpers --------------------------------------------------------------------------------------------------
    template<typename Predicate>
    QQuickItem *findItem(QQuickItem *root, Predicate predicate)
    {
        if (!root) {
            return nullptr;
        }
        if (root->isVisible() && predicate(root)) {
            return root;
        }
        for (QQuickItem *child : root->childItems()) {
            if (QQuickItem *found = findItem(child, predicate)) {
                return found;
            }
        }
        return nullptr;
    }
    QQuickItem *byName(const QString &name)
    {
        return findItem(m_window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
    }
    QQuickItem *byText(const QString &text)
    {
        return findItem(m_window->contentItem(), [&text](QQuickItem *item) {
            return item->property("text").toString() == text && item->property("enabled").toBool();
        });
    }
    QPoint centre(QQuickItem *item, QPointF offset = {})
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2) + offset).toPoint();
    }
    void click(QQuickItem *item, QPointF offset = {})
    {
        QVERIFY2(item, "item to click not found");
        QTest::mouseClick(m_window, Qt::LeftButton, {}, centre(item, offset));
        ++m_actions;
    }
    void key(int key, Qt::KeyboardModifiers modifiers = {})
    {
        QTest::keyClick(m_window, static_cast<Qt::Key>(key), modifiers);
        ++m_actions;
    }
    void drag(QPoint from, QPoint to)
    {
        QTest::mousePress(m_window, Qt::LeftButton, {}, from);
        const int steps = 12;
        for (int i = 1; i <= steps; ++i) {
            QTest::mouseMove(m_window, from + (to - from) * i / steps);
            QTest::qWait(5);
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, to);
        ++m_actions;
    }
    void shot(const QString &name)
    {
        const QString folder = qEnvironmentVariable("VEDIT_UI_SHOTS");
        if (!folder.isEmpty()) {
            QTest::qWait(300);
            QDir().mkpath(folder);
            m_window->grabWindow().save(folder + u"/"_s + name + u".png"_s);
        }
    }
    ui::EditorController *editor() const { return m_app->editor(); }
    const Track &mainTrack() const { return editor()->data().mainSequence()->visualTracks.front(); }
    // Scene x of a frame on the timeline.
    int frameX(int frame)
    {
        QQuickItem *canvas = byName(u"timelineCanvas"_s);
        const double zoom = byName(u"timelineCanvas"_s)->parentItem()->parentItem()->parentItem()->property("zoom").toDouble();
        return static_cast<int>(canvas->mapToScene(QPointF(frame * zoom, 0)).x());
    }

private slots:
    void initTestCase()
    {
        m_files = generateTestMedia(m_dir.filePath(u"media"_s));
        if (!m_files.ok) {
            QSKIP("ffmpeg is needed to generate the test media");
        }
        // A music library with one song (usability test 2).
        QDir().mkpath(m_dir.filePath(u"music"_s));
        QVERIFY(QFile::copy(m_files.music, m_dir.filePath(u"music/song.mp3"_s)));
        qputenv("VEDIT_MUSIC_DIR", QFile::encodeName(m_dir.filePath(u"music"_s)));
        QVERIFY(engine::MltRuntime::waitUntilReady());

        static QStringList *warnings = &m_warnings;
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
            if (type >= QtWarningMsg && message.contains(u"qml"_s, Qt::CaseInsensitive)) {
                warnings->append(message);
            }
            fprintf(stderr, "%s\n", qPrintable(message));
        });

        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        m_appearance = std::make_unique<theme::SystemAppearance>();
        theme::ThemeManager::loadFonts();
        m_theme = std::make_unique<theme::ThemeManager>(m_appearance.get());
        theme::ThemeManager::setInstance(m_theme.get());
        m_theme->setSessionOverrides(theme::ThemeManager::Mode::Dark, std::nullopt);
        m_theme->setSoftwareRendering(true);
        QQuickStyle::setStyle(u"Vedit.Style"_s);
        m_app = std::make_unique<ui::AppController>(gpu::GraphicsDecision{}, gpu::GpuCapabilities{},
                                                    QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        ui::AppController::setInstance(m_app.get());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadFromModule("Vedit.UI", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
        m_window->resize(1440, 900);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        shot(u"01-home-empty"_s);
    }

    // Usability test 1: from the start to the first cut of a clip (new project, drag a clip, split) ≤ 4 actions.
    void usability1FirstCut()
    {
        m_actions = 0;
        click(byName(u"newProjectButton"_s));                                          // 1
        QTRY_VERIFY(editor());
        editor()->player()->setVolume(0.0);
        // 2: the file dragged from the file manager onto the timeline. Platform drag and drop cannot run headless:
        // the drop handler's call is made directly, and counted as the single drag it is.
        editor()->importAndInsert({QUrl::fromLocalFile(m_files.landscape)}, 0, editor()->timeline()->mainRow());
        ++m_actions;
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack().clips.size(), size_t(1), 20000);
        shot(u"02-editor-first-clip"_s);
        click(byName(u"timelineCanvas"_s), {});                                        // 3: playhead where to cut
        QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(frameX(60), centre(byName(u"timelineCanvas"_s)).y()));
        QTRY_VERIFY(std::abs(editor()->player()->position() - 60) <= 1);
        click(byName(u"splitButton"_s));                                               // 4
        QTRY_COMPARE(mainTrack().clips.size(), size_t(2));
        qInfo("usability test 1: %d actions", m_actions);
        QVERIFY(m_actions <= 4);
        shot(u"03-editor-split"_s);
    }

    // Usability test 2: music from the local library under the video ≤ 2 actions.
    void usability2Music()
    {
        m_actions = 0;
        click(byText(u"Audio"_s));                                                     // 1
        QTRY_VERIFY_WITH_TIMEOUT(byName(u"addMusic-0"_s) && byName(u"addMusic-0"_s)->property("enabled").toBool(), 20000);
        shot(u"04-editor-music-library"_s);
        click(byName(u"addMusic-0"_s));                                                // 2
        QTRY_COMPARE(editor()->data().mainSequence()->audioTracks.size(), size_t(1));
        qInfo("usability test 2: %d actions", m_actions);
        QVERIFY(m_actions <= 2);
    }

    void timelineGestures()
    {
        // Drag the second clip before the first: the magnetic track reorders.
        const ClipId second = mainTrack().clips[1].id;
        QQuickItem *clip = byName(u"clip-"_s + second.toString());
        QVERIFY(clip);
        drag(centre(clip), QPoint(frameX(2), centre(clip).y()));
        QTRY_COMPARE(mainTrack().clips.front().id, second);

        // Trim the end of the (now) second clip by dragging its edge left.
        const ClipId trimmed = mainTrack().clips[1].id;
        const RationalTime before = editor()->data().findClip(trimmed)->duration;
        QQuickItem *edge = findItem(byName(u"clip-"_s + trimmed.toString()),
                                    [](QQuickItem *item) { return item->objectName() == u"trimEnd"_s; });
        QVERIFY(edge);
        drag(centre(edge), centre(edge) - QPoint(frameX(20) - frameX(0), 0));
        QTRY_VERIFY(editor()->data().findClip(trimmed)->duration < before);
        QVERIFY(std::abs(before.value() - editor()->data().findClip(trimmed)->duration.value() - 20) <= 1);

        // Select, delete with the keyboard, undo from the snackbar.
        click(byName(u"clip-"_s + trimmed.toString()));
        QCOMPARE(editor()->selection(), QStringList{trimmed.toString()});
        key(Qt::Key_Delete);
        QTRY_COMPARE(mainTrack().clips.size(), size_t(1));
        QTRY_VERIFY(byText(u"Undo"_s)); // the snackbar fades in
        shot(u"05-editor-deleted-snackbar"_s);
        click(byText(u"Undo"_s));
        QTRY_COMPARE(mainTrack().clips.size(), size_t(2));
    }

    // Usability test 8: export with the recommended settings ≤ 2 actions.
    void usability8Export()
    {
        m_actions = 0;
        QSignalSpy exported(editor(), &ui::EditorController::exportFinished);
        click(byName(u"exportButton"_s));                                              // 1
        QTRY_VERIFY(byName(u"exportConfirmButton"_s));
        shot(u"06-export-dialog"_s);
        click(byName(u"exportConfirmButton"_s));                                       // 2
        QVERIFY(exported.wait(60000));
        qInfo("usability test 8: %d actions", m_actions);
        QVERIFY(m_actions <= 2);
        const QString output = exported.first().first().toString();
        QVERIFY(QFileInfo(output).size() > 0);
        QCOMPARE(streamOfType(ffprobe(output), u"video"_s).value(u"codec_name"_s).toString(), u"h264"_s);
        shot(u"07-export-done"_s);
        QFile::remove(output); // the dev sandbox's Videos folder stays clean
        click(byText(u"Close"_s));
        QTRY_VERIFY(!byName(u"exportConfirmButton"_s) && !byText(u"Copy path"_s));
    }

    void backHomeShowsTheDraft()
    {
        const QString name = editor()->name();
        click(byName(u"backButton"_s));
        QTRY_VERIFY(!editor());
        QTRY_VERIFY(byText(name));
        shot(u"08-home-draft"_s);
        m_theme->setSessionOverrides(theme::ThemeManager::Mode::Light, std::nullopt);
        shot(u"09-home-draft-light"_s);
        m_theme->setSessionOverrides(theme::ThemeManager::Mode::Dark, std::nullopt);
    }

    void draftActionsOnTheHomeScreen()
    {
        QTRY_VERIFY(!editor());
        const int drafts = m_app->drafts()->count();
        QVERIFY(drafts >= 1);
        // The card's menu appears when pointing at it (cards are recreated when the list changes: found again).
        const auto card = [this] {
            return findItem(m_window->contentItem(), [](QQuickItem *item) {
                return QByteArray(item->metaObject()->className()).startsWith("DraftCard");
            });
        };
        QVERIFY(card());
        QTest::mouseMove(m_window, centre(card()));
        QTRY_VERIFY(byName(u"draftMenuButton"_s));
        click(byName(u"draftMenuButton"_s));
        QTRY_VERIFY(byText(u"Duplicate"_s));
        click(byText(u"Duplicate"_s));
        QTRY_COMPARE(m_app->drafts()->count(), drafts + 1);
        QTest::mouseMove(m_window, QPoint(0, 0));
        QTest::mouseMove(m_window, centre(card()));
        QTRY_VERIFY(byName(u"draftMenuButton"_s));
        click(byName(u"draftMenuButton"_s));
        QTRY_VERIFY(byText(u"Move to trash"_s));
        click(byText(u"Move to trash"_s));
        QTRY_COMPARE(m_app->drafts()->count(), drafts);
    }

    void noQmlWarnings()
    {
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(u'\n')));
    }

    void cleanupTestCase()
    {
        if (m_app) {
            m_app->closeEditor();
        }
        m_engine.reset();
        m_app.reset();
        m_theme.reset();
        engine::MltRuntime::shutdown();
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
