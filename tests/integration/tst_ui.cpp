// SPDX-License-Identifier: GPL-3.0-or-later
// The real interface driven with the mouse and the keyboard, headless (software scene graph): the usability tests of
// SPEC §0bis counted in actions (clicks, keys, drags), and the timeline gestures. Set VEDIT_UI_SHOTS=<folder> to also
// save screenshots of each step (visual review).
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "theme/SystemAppearance.h"
#include "theme/ThemeManager.h"
#include "document/Document.h"
#include "ui/controllers/AppController.h"
#include "ui/controllers/ClipInspector.h"
#include "ui/controllers/EditorController.h"
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
    // Scrolls the nearest scrollable parent so that `item` is fully visible (what a user does with the wheel).
    void ensureVisible(QQuickItem *item)
    {
        for (QQuickItem *parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (!QByteArray(parent->metaObject()->className()).contains("Flickable")) {
                continue;
            }
            const QRectF box = item->mapRectToItem(parent, QRectF(0, 0, item->width(), item->height()));
            const double contentY = parent->property("contentY").toDouble();
            if (box.bottom() > parent->height()) {
                parent->setProperty("contentY", contentY + box.bottom() - parent->height());
            } else if (box.top() < 0) {
                parent->setProperty("contentY", contentY + box.top());
            }
            QTest::qWait(50);
            return;
        }
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
        // The pointer gets there first, as a hand does (hover ends elsewhere: tooltips close).
        QTest::mouseMove(m_window, from);
        QTest::qWait(20);
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

        // No translation is loaded: the interface is in English (the source strings), so are the library names.
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
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

    void dragMediaAndContextMenu()
    {
        // A media tile dragged from the pool onto the end of the main track.
        click(byText(u"Media"_s));
        const size_t before = mainTrack().clips.size();
        QQuickItem *tile = findItem(m_window->contentItem(), [](QQuickItem *item) {
            return QByteArray(item->metaObject()->className()).startsWith("MediaTile") &&
                   item->property("kind").toString() == u"video"_s;
        });
        QVERIFY(tile);
        QQuickItem *canvas = byName(u"timelineCanvas"_s);
        const int mainY = static_cast<int>(canvas->mapToScene(QPointF(0, 0)).y()) + 24 + 32; // new-track zone + half track
        drag(centre(tile), QPoint(frameX(editor()->timeline()->duration() + 30), mainY));
        QTRY_COMPARE(mainTrack().clips.size(), before + 1);

        // Right click on a clip: the toolbar's actions.
        const ClipId last = mainTrack().clips.back().id;
        QQuickItem *clip = byName(u"clip-"_s + last.toString());
        QVERIFY(clip);
        QTest::mouseClick(m_window, Qt::RightButton, {}, centre(clip));
        QTRY_VERIFY(byName(u"clipMenu"_s) || byText(u"Duplicate"_s));
        click(byText(u"Duplicate"_s));
        QTRY_COMPARE(mainTrack().clips.size(), before + 2);
        editor()->undo();
        editor()->undo();
        QTRY_COMPARE(mainTrack().clips.size(), before);
    }

    // The properties panel on the right: tabs by clip kind, a slider drag is one undo step, auto enhance.
    void propertiesPanel()
    {
        const ClipId first = mainTrack().clips.front().id;
        click(byName(u"clip-"_s + first.toString()));
        QTRY_VERIFY(byName(u"propertiesTabs"_s));
        shot(u"10-properties-video"_s);

        // Opacity from 100 % to about half, with a drag of the slider: one undo step.
        QQuickItem *slider = byName(u"slider_opacity"_s);
        QVERIFY(slider);
        const int steps = editor()->document().undoStack().index();
        const QPoint end = slider->mapToScene(QPointF(slider->width() - 12, slider->height() / 2)).toPoint();
        drag(end, slider->mapToScene(QPointF(slider->width() / 2, slider->height() / 2)).toPoint());
        QTRY_VERIFY(std::abs(std::get<double>(editor()->data().findClip(first)->opacity.staticValue()) - 0.5) < 0.1);
        QCOMPARE(editor()->document().undoStack().index(), steps + 1);
        QTRY_VERIFY(byName(u"reset_video"_s));
        click(byName(u"reset_video"_s));
        QTRY_COMPARE(std::get<double>(editor()->data().findClip(first)->opacity.staticValue()), 1.0);

        // Adjust: auto enhance fills in the adjustments.
        click(byText(u"Adjust"_s));
        QTRY_VERIFY(byName(u"autoEnhanceButton"_s));
        click(byName(u"autoEnhanceButton"_s));
        QTRY_VERIFY(editor()->inspector()->modifiedSections().contains(u"adjust"_s));
        shot(u"11-properties-adjust"_s);
        editor()->undo();

        // A text: added from the toolbar, written in the panel.
        editor()->clearSelection();
        // The toolbar rebuilds its buttons for the new selection: once they are laid out.
        QTRY_VERIFY(byName(u"addTextButton"_s));
        QTest::qWait(50);
        click(byName(u"addTextButton"_s));
        QTRY_COMPARE(editor()->inspector()->kind(), int(ui::ClipInspector::Text));
        QQuickItem *content = byName(u"textContent"_s);
        QVERIFY(content);
        click(content);
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        for (const char c : {'C', 'i', 'a', 'o'}) {
            QTest::keyClick(m_window, c);
        }
        QTRY_COMPARE(editor()->inspector()->values().value(u"text.content"_s).toString(), u"Ciao"_s);
        shot(u"12-properties-text"_s);
        editor()->undo();
        editor()->undo();
    }

    // Usability test 3: a filter on every clip ≤ 3 actions.
    void usability3FilterOnAllClips()
    {
        // Three clips on the main track (setup, not counted).
        editor()->clearSelection();
        editor()->player()->seek(0);
        while (mainTrack().clips.size() < 3) {
            QVERIFY(editor()->addMedia(mainTrack().clips.front().media()->mediaId.toString()));
        }
        editor()->clearSelection();
        editor()->player()->seek(1);
        m_actions = 0;
        click(byText(u"Filters"_s));                                                   // 1
        QTRY_VERIFY(byName(u"asset_filters/vivid"_s));
        // Pointing at a filter previews it; nothing changes in the project.
        const int steps = editor()->document().undoStack().index();
        QTest::mouseMove(m_window, centre(byName(u"asset_filters/pop"_s)));
        QTest::qWait(100);
        QCOMPARE(editor()->document().undoStack().index(), steps);
        shot(u"13-library-filters"_s);
        click(byName(u"asset_filters/vivid"_s));                                       // 2: the clip on screen
        QTRY_VERIFY(byName(u"filterApplyAll"_s));
        click(byName(u"filterApplyAll"_s));                                            // 3
        qInfo("usability test 3: %d actions", m_actions);
        QVERIFY(m_actions <= 3);
        for (const Clip &clip : mainTrack().clips) {
            QVERIFY(std::any_of(clip.effects.begin(), clip.effects.end(), [](const Effect &effect) {
                return effect.type == u"vedit.filter"_s && effect.preset && effect.preset->id == u"filters/vivid"_s;
            }));
        }
    }

    // Usability test 6: a transition between all the clips ≤ 3 actions.
    void usability6TransitionOnAllCuts()
    {
        editor()->clearSelection();
        m_actions = 0;
        click(byText(u"Transitions"_s));                                               // 1
        QTRY_VERIFY(byName(u"asset_transitions/dissolve"_s));
        QTest::mouseMove(m_window, centre(byName(u"asset_transitions/slide-left"_s)));
        QTest::qWait(300);
        shot(u"14-library-transitions"_s);
        click(byName(u"asset_transitions/dissolve"_s));                                // 2: the cut nearest the playhead
        QTRY_VERIFY(byName(u"transitionApplyAll"_s));
        click(byName(u"transitionApplyAll"_s));                                        // 3
        qInfo("usability test 6: %d actions", m_actions);
        QVERIFY(m_actions <= 3);
        size_t cuts = 0;
        for (size_t i = 0; i + 1 < mainTrack().clips.size(); ++i) {
            cuts += mainTrack().clips[i].end() == mainTrack().clips[i + 1].start ? 1 : 0;
        }
        QVERIFY(cuts >= 2);
        QCOMPARE(mainTrack().transitions.size(), cuts);
        shot(u"15-transitions-on-timeline"_s);

        // The duration from the edge of the mark on the timeline.
        const TransitionId first = mainTrack().transitions.front().id;
        const RationalTime before = mainTrack().transitions.front().duration;
        QQuickItem *mark = byName(u"cut-"_s + mainTrack().transitions.front().from.toString());
        QVERIFY(mark);
        click(mark);
        QTRY_VERIFY(findItem(mark, [](QQuickItem *item) { return item->objectName() == u"transitionEnd"_s; }));
        QQuickItem *edge = findItem(mark, [](QQuickItem *item) { return item->objectName() == u"transitionEnd"_s; });
        drag(centre(edge), centre(edge) + QPoint(frameX(10) - frameX(0), 0));
        QTRY_VERIFY(mainTrack().transitions.front().duration > before);
        QCOMPARE(mainTrack().transitions.front().id, first);
        editor()->undo(); // the duration
        editor()->undo(); // apply to all
        editor()->undo(); // the first transition
        editor()->undo(); // the filter on all
        editor()->undo(); // the filter
        click(byText(u"Media"_s));
    }

    // The Text library: a click adds a text in that style; with a text selected, a click restyles it.
    void textLibrary()
    {
        editor()->clearSelection();
        click(byText(u"Text"_s));
        QTRY_VERIFY(byName(u"asset_text/outline-yellow"_s));
        click(byName(u"asset_text/outline-yellow"_s));
        QTRY_COMPARE(editor()->inspector()->kind(), int(ui::ClipInspector::Text));
        QCOMPARE(editor()->inspector()->values().value(u"text.preset"_s).toString(), u"text/outline-yellow"_s);
        click(byName(u"asset_text/label-black"_s));
        QTRY_COMPARE(editor()->inspector()->values().value(u"text.preset"_s).toString(), u"text/label-black"_s);
        shot(u"16-library-text"_s);
        editor()->undo();
        editor()->undo();
        click(byText(u"Media"_s));
    }

    // Handles on the preview: move, resize, rotate; a text edited in place; the format under the player.
    void canvasHandles()
    {
        const ClipId first = mainTrack().clips.front().id;
        editor()->player()->pause();
        editor()->player()->seek(10);
        QTRY_COMPARE(editor()->player()->position(), 10);
        click(byName(u"clip-"_s + first.toString()));
        // Pointing at the timeline skims (the handles hide): the pointer goes to the player.
        QTest::mouseMove(m_window, centre(byName(u"propertiesPanel"_s)) - QPoint(500, 0));
        QTRY_VERIFY(!editor()->player()->skimming());
        QTRY_VERIFY(byName(u"canvasBox"_s));
        const auto value = [&](const char *key) { return editor()->inspector()->values().value(QString::fromLatin1(key)).toDouble(); };
        const int steps = editor()->document().undoStack().index();

        QQuickItem *move = byName(u"canvasMove"_s);
        drag(centre(move), centre(move) + QPoint(60, 0));
        QTRY_VERIFY(value("x") > 0.05);
        QCOMPARE(editor()->document().undoStack().index(), steps + 1);
        // Back near the centre: it snaps onto the centre line.
        drag(centre(move), centre(move) - QPoint(58, 0));
        QTRY_COMPARE(value("x"), 0.0);

        QQuickItem *corner = byName(u"canvasCorner3"_s);
        QVERIFY(corner);
        drag(centre(corner), centre(corner) + QPoint(30, 20));
        QTRY_VERIFY(value("scale") > 1.05);
        QQuickItem *rotate = byName(u"canvasRotate"_s);
        QVERIFY(rotate);
        const QPoint boxCentre = centre(byName(u"canvasBox"_s));
        drag(centre(rotate), boxCentre + QPoint(200, -40));
        QTRY_VERIFY(std::abs(value("rotation")) > 20);
        shot(u"17-canvas-handles"_s);
        while (editor()->document().undoStack().index() > steps) {
            editor()->undo();
        }

        // A text: double click on it, type, Escape.
        editor()->clearSelection();
        // The toolbar rebuilds its buttons for the new selection: once they are laid out.
        QTRY_VERIFY(byName(u"addTextButton"_s));
        QTest::qWait(50);
        click(byName(u"addTextButton"_s));
        QTRY_COMPARE(editor()->inspector()->kind(), int(ui::ClipInspector::Text));
        QTest::mouseMove(m_window, centre(byName(u"propertiesPanel"_s)) - QPoint(500, 0));
        QTRY_VERIFY(byName(u"canvasMove"_s));
        QTest::mouseDClick(m_window, Qt::LeftButton, {}, centre(byName(u"canvasMove"_s)));
        QTRY_VERIFY(byName(u"canvasTextEditor"_s));
        for (const char c : {'H', 'i'}) {
            QTest::keyClick(m_window, c);
        }
        QTRY_COMPARE(editor()->inspector()->values().value(u"text.content"_s).toString(), u"Hi"_s);
        shot(u"18-canvas-text-editing"_s);
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!byName(u"canvasTextEditor"_s));
        editor()->undo();
        editor()->undo();

        // The format, under the player: 9:16 in two clicks.
        click(byName(u"formatButton"_s));
        QTRY_VERIFY(byName(u"format_1"_s));
        click(byName(u"format_1"_s));
        QTRY_COMPARE(editor()->canvasPreset(), 1);
        shot(u"19-format-9x16"_s);
        editor()->undo();
        QTRY_COMPARE(editor()->canvasPreset(), 0);
        QTRY_VERIFY(!byName(u"format_1"_s)); // the menu has finished closing (it takes the keys until then)
    }

    // Ctrl+K: type, Enter applies the first result (SPEC 0bis rule 14); toolbar buttons open their page.
    void universalSearchAndToolbar()
    {
        editor()->clearSelection();
        editor()->player()->seek(10);
        const int steps = editor()->document().undoStack().index();
        key(Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(byName(u"universalSearchField"_s));
        for (const char c : {'v', 'i', 'v', 'i', 'd'}) {
            QTest::keyClick(m_window, c);
        }
        QTRY_VERIFY(byName(u"searchResult_0"_s));
        shot(u"20-universal-search"_s);
        key(Qt::Key_Return);
        QTRY_VERIFY(!byName(u"universalSearchField"_s));
        QTRY_COMPARE(editor()->inspector()->values().value(u"filter"_s).toString(), u"filters/vivid"_s);
        while (editor()->document().undoStack().index() > steps) {
            editor()->undo();
        }

        // "Speed" in the toolbar of a video clip shows the Speed page of the properties.
        click(byName(u"clip-"_s + mainTrack().clips.front().id.toString()));
        QTRY_VERIFY(byName(u"speedButton"_s));
        QTest::qWait(50);
        click(byName(u"speedButton"_s));
        QTRY_VERIFY(byName(u"reverseSwitch"_s));
        // The tab shown is the Speed one (Video, Audio, Speed, Adjust), even after another tab was clicked before.
        QCOMPARE(byName(u"propertiesTabs"_s)->property("currentIndex").toInt(), 2);
        shot(u"21-toolbar-speed"_s);
    }

    // The mixer: a click on a track header opens its volume and mute; the meters move while the video plays.
    void mixer()
    {
        const int mainRow = editor()->timeline()->mainRow();
        click(byName(u"trackHeader_%1"_s.arg(mainRow)));
        QTRY_VERIFY(byName(u"trackVolume"_s));
        const int steps = editor()->document().undoStack().index();
        QQuickItem *slider = byName(u"trackVolume"_s);
        drag(slider->mapToScene(QPointF(slider->width() * 60 / 72, slider->height() / 2)).toPoint(),
             slider->mapToScene(QPointF(slider->width() * 30 / 72, slider->height() / 2)).toPoint());
        QTRY_VERIFY(mainTrack().gainDb.numberAt(RationalTime(), 0.0) < -10);
        QCOMPARE(editor()->document().undoStack().index(), steps + 1);
        click(byName(u"trackMute"_s));
        QTRY_VERIFY(mainTrack().muted);
        shot(u"22-track-mixer"_s);
        key(Qt::Key_Escape);
        QTRY_VERIFY(!byName(u"trackMixer"_s) || !byName(u"trackVolume"_s));
        while (editor()->document().undoStack().index() > steps) {
            editor()->undo();
        }
        QVERIFY(!mainTrack().muted);

        // Playing: the master meter shows the sound of the video.
        editor()->player()->seek(0);
        editor()->player()->play();
        QQuickItem *master = byName(u"masterMeter"_s);
        QVERIFY(master);
        QTRY_VERIFY_WITH_TIMEOUT(master->property("level").toDouble() > 0.01, 5000);
        editor()->player()->pause();
    }

    // The Animations library: a click animates the clip on screen.
    void animationsLibrary()
    {
        editor()->clearSelection();
        editor()->player()->seek(10);
        const int steps = editor()->document().undoStack().index();
        click(byText(u"Animations"_s));
        QTRY_VERIFY(byName(u"asset_animations/in/zoom_in"_s));
        QTest::mouseMove(m_window, centre(byName(u"asset_animations/in/spin"_s)));
        QTest::qWait(300);
        shot(u"23-library-animations"_s);
        click(byName(u"asset_animations/in/zoom_in"_s));
        QTRY_COMPARE(editor()->inspector()->values().value(u"animation.in"_s).toString(), u"animations/in/zoom_in"_s);
        while (editor()->document().undoStack().index() > steps) {
            editor()->undo();
        }
        click(byText(u"Media"_s));
    }

    // Markers: M adds one at the playhead (of the video, or of the selected clip); click goes there, right click removes.
    void markers()
    {
        editor()->clearSelection();
        editor()->player()->seek(30);
        QTRY_COMPARE(editor()->player()->position(), 30);
        key(Qt::Key_M);
        QTRY_COMPARE(editor()->data().mainSequence()->markers.size(), size_t(1));
        const QString id = editor()->data().mainSequence()->markers.front().id.toString();
        QTRY_VERIFY(byName(u"marker_"_s + id));
        editor()->player()->seek(90);
        click(byName(u"marker_"_s + id));
        QTRY_COMPARE(editor()->player()->position(), 30);
        QTest::mouseClick(m_window, Qt::RightButton, {}, centre(byName(u"marker_"_s + id)));
        QTRY_VERIFY(editor()->data().mainSequence()->markers.empty());

        const ClipId first = mainTrack().clips.front().id;
        click(byName(u"clip-"_s + first.toString()));
        editor()->player()->seek(20);
        key(Qt::Key_M);
        QTRY_COMPARE(editor()->data().findClip(first)->markers.size(), size_t(1));
        const QString tick = u"clipMarker_"_s + editor()->data().findClip(first)->markers.front().id.toString();
        QTRY_VERIFY(byName(tick));
        shot(u"24-markers"_s);
        editor()->undo();
        QTRY_VERIFY(!byName(tick));
    }

    // The Phase 3 criterion, first half (SPEC §8): a title animated with keyframes and easing, through the interface.
    void phaseThreeCriterionTitle()
    {
        editor()->clearSelection();
        editor()->player()->seek(0);
        QTRY_COMPARE(editor()->player()->position(), 0);
        QTRY_VERIFY(byName(u"addTextButton"_s));
        QTest::qWait(50);
        const int steps = editor()->document().undoStack().index();
        click(byName(u"addTextButton"_s));
        QTRY_COMPARE(editor()->inspector()->kind(), int(ui::ClipInspector::Text));
        const ClipId title = *ClipId::fromString(editor()->inspector()->clipId());
        click(byText(u"Position"_s));
        QTRY_VERIFY(byName(u"keyframe_opacity"_s));
        // A keyframe at the start…
        click(byName(u"keyframe_opacity"_s));
        QTRY_VERIFY(editor()->data().findClip(title)->opacity.isAnimated());
        // …one second later, a lower opacity: a second keyframe by itself.
        editor()->player()->seek(30);
        QTRY_COMPARE(editor()->inspector()->values().value(u"kf.opacity"_s).toInt(), 1);
        QQuickItem *slider = byName(u"slider_opacity"_s);
        QVERIFY(slider);
        ensureVisible(slider);
        // The diamond's tooltip (under it, over the slider) takes presses until it has faded: a hand is slower than that.
        QTest::mouseMove(m_window, slider->mapToScene(QPointF(slider->width() - 12, slider->height() / 2)).toPoint());
        QTRY_VERIFY(!findItem(m_window->contentItem(), [](QQuickItem *item) { return item->objectName() == u"ToolTip"_s; }));
        drag(slider->mapToScene(QPointF(slider->width() - 12, slider->height() / 2)).toPoint(),
             slider->mapToScene(QPointF(slider->width() * 0.2, slider->height() / 2)).toPoint());
        QTRY_COMPARE(editor()->data().findClip(title)->opacity.keyframes().size(), size_t(2));
        // Easing: back on the first keyframe, "Smooth".
        ensureVisible(byName(u"previousKeyframe"_s));
        click(byName(u"previousKeyframe"_s));
        QTRY_COMPARE(editor()->player()->position(), 0);
        QTRY_VERIFY(byName(u"easing_easeInOut"_s));
        ensureVisible(byName(u"easing_easeInOut"_s));
        click(byName(u"easing_easeInOut"_s));
        QTRY_COMPARE(editor()->data().findClip(title)->opacity.keyframes().front().easing.name(), u"easeInOut"_s);
        QCOMPARE(editor()->data().findClip(title)->opacity.keyframes().front().interpolation, Interpolation::Bezier);
        QTRY_VERIFY(byName(u"clipKeyframe_30"_s));
        shot(u"25-title-keyframes"_s);
        while (editor()->document().undoStack().index() > steps) {
            editor()->undo();
        }
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
