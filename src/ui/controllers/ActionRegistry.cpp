// SPDX-License-Identifier: GPL-3.0-or-later
#include "ActionRegistry.h"

#include "ClipInspector.h"
#include "EditorController.h"
#include "core/project/ProjectData.h"
#include "fx/Library.h"
#include "models/AudioLibraryModel.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {

// Lower case without accents: "velocita" finds "Velocità".
QString folded(const QString &text)
{
    QString result;
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    result.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            result.append(c.toCaseFolded());
        }
    }
    return result;
}

// 0 = no match; higher = better: the whole name, its start, the start of a word, anywhere.
int score(const QString &needle, const QStringList &names)
{
    int best = 0;
    for (const QString &name : names) {
        const QString haystack = folded(name);
        if (haystack.isEmpty()) {
            continue;
        }
        if (haystack == needle) {
            best = std::max(best, 4);
        } else if (haystack.startsWith(needle)) {
            best = std::max(best, 3);
        } else if (haystack.contains(u' ' + needle) || haystack.contains(u'-' + needle)) {
            best = std::max(best, 2);
        } else if (haystack.contains(needle)) {
            best = std::max(best, 1);
        }
    }
    return best;
}

} // namespace

ActionRegistry::ActionRegistry(EditorController &editor)
    : QObject(&editor)
    , m_editor(editor)
{
    ClipInspector *inspector = editor.inspector();
    const auto always = [] { return true; };
    const auto clip = [this] { return m_editor.focusClip().has_value(); };
    const auto properties = [this](const QString &page) {
        return [this, page] {
            emit m_editor.propertiesRequested(page);
            return true;
        };
    };
    const auto library = [this](const QString &name) {
        return [this, name] {
            emit m_editor.libraryRequested(name);
            return true;
        };
    };
    const int videoLike = Video | Image;

    // The contextual toolbar, in order (SPEC 0bis rule 3). Features of later phases get their entry with them.
    add({u"split"_s, tr("Split"), u"content_cut"_s, u"S"_s, AnyClip | Nothing, [this] { return m_editor.canSplit(); },
         [this] { return m_editor.split(); }});
    add({u"textEdit"_s, tr("Edit text"), u"edit"_s, {}, Text, clip, properties(u"text"_s)});
    add({u"textStyle"_s, tr("Text style"), u"style"_s, {}, Text, always, library(u"text"_s)});
    add({u"delete"_s, tr("Delete"), u"delete"_s, tr("Del"), AnyClip | TransitionSelected,
         [this] { return m_editor.focusClip().has_value() || m_editor.focusTransition().has_value(); },
         [this, inspector] {
             return m_editor.focusTransition() && !m_editor.focusClip() ? inspector->removeTransition()
                                                                        : m_editor.deleteSelection();
         }});
    add({u"duplicate"_s, tr("Duplicate"), u"content_copy"_s, tr("Ctrl+D"), AnyClip, clip,
         [this] { return m_editor.duplicateSelection(); }});
    add({u"speed"_s, tr("Speed"), u"speed"_s, {}, Video | Audio, clip, properties(u"speed"_s)});
    add({u"volume"_s, tr("Volume"), u"volume_up"_s, {}, Video | Audio, clip, properties(u"audio"_s)});
    add({u"fades"_s, tr("Fades"), u"graphic_eq"_s, {}, Audio, clip, properties(u"audio"_s)});
    add({u"animation"_s, tr("Animation"), u"animation"_s, {}, videoLike | Text, clip, library(u"animations"_s)});
    add({u"freeze"_s, tr("Freeze"), u"ac_unit"_s, {}, Video | Nothing, always, [this] { return m_editor.freezeFrame(); }});
    add({u"reverse"_s, tr("Reverse"), u"swap_horiz"_s, {}, Video, clip, [inspector] {
             return inspector->set(u"reversed"_s, !inspector->values().value(u"reversed"_s).toBool());
         }});
    add({u"mirror"_s, tr("Mirror"), u"flip"_s, {}, videoLike, clip, [inspector] {
             return inspector->set(u"flipH"_s, !inspector->values().value(u"flipH"_s).toBool());
         }});
    add({u"rotate"_s, tr("Rotate"), u"rotate_right"_s, {}, videoLike | Text, clip, [inspector] {
             const double rotation = inspector->values().value(u"rotation"_s).toDouble() + 90.0;
             const bool done = inspector->set(u"rotation"_s, rotation > 180.0 ? rotation - 360.0 : rotation);
             inspector->endGesture();
             return done;
         }});
    add({u"enhance"_s, tr("Enhance"), u"auto_fix_high"_s, {}, videoLike | Audio, clip,
         [inspector] { return inspector->autoEnhance(); }});
    add({u"transitionAll"_s, tr("Apply to all cuts"), u"transition_fade"_s, {}, TransitionSelected, always,
         [inspector] { return inspector->applyToAll(u"transition"_s); }});
    add({u"addText"_s, tr("Add text"), u"title"_s, {}, Nothing, always, [this] { return m_editor.addText(); }});
    add({u"addAudio"_s, tr("Add audio"), u"music_note"_s, {}, Nothing, always, library(u"audio"_s)});

    // Found by the search (and, some, in the right-click menu of the timeline).
    add({u"copyAttributes"_s, tr("Copy attributes"), u"format_paint"_s, tr("Ctrl+Alt+C"), 0, clip, [inspector] {
             inspector->copyAttributes();
             return true;
         }});
    add({u"pasteAttributes"_s, tr("Paste attributes"), u"content_paste"_s, tr("Ctrl+Alt+V"), 0,
         [this, inspector] { return inspector->canPaste() && !m_editor.selectedClips().empty(); },
         [inspector] { return inspector->pasteAttributes(); }});
    add({u"createCompound"_s, tr("Group into a compound clip"), u"stacks"_s, {}, 0,
         [this] { return !m_editor.selectedClips().empty(); }, [this] { return m_editor.createCompoundClip(); }});
    add({u"expandCompound"_s, tr("Ungroup the compound clip"), u"stacks"_s, {}, 0,
         [this] {
             const std::optional<ClipId> focus = m_editor.focusClip();
             const Clip *clip = focus ? m_editor.data().findClip(*focus) : nullptr;
             return clip && std::holds_alternative<CompoundClipData>(clip->payload);
         },
         [this] { return m_editor.expandCompoundClip(); }});
    add({u"addAdjustment"_s, tr("Add an adjustment layer"), u"tune"_s, {}, 0, always,
         [this] { return m_editor.insertAdjustment(); }});
    add({u"undo"_s, tr("Undo"), u"undo"_s, tr("Ctrl+Z"), 0, [this] { return m_editor.canUndo(); }, [this] {
             m_editor.undo();
             return true;
         }});
    add({u"redo"_s, tr("Redo"), u"redo"_s, tr("Ctrl+Shift+Z"), 0, [this] { return m_editor.canRedo(); }, [this] {
             m_editor.redo();
             return true;
         }});
    add({u"export"_s, tr("Export the video"), u"file_upload"_s, tr("Ctrl+E"), 0, always, [this] {
             emit m_editor.exportRequested();
             return true;
         }});
    add({u"import"_s, tr("Import media"), u"add_photo_alternate"_s, tr("Ctrl+I"), 0, always, [this] {
             emit m_editor.importRequested();
             return true;
         }});
    add({u"addMarker"_s, tr("Add a marker"), u"bookmark_add"_s, u"M"_s, 0, always, [this] { return m_editor.addMarker(); }});
    add({u"nextMarker"_s, tr("Next marker"), u"bookmark"_s, {}, 0, always, [this] {
             m_editor.nextMarker();
             return true;
         }});
    add({u"previousMarker"_s, tr("Previous marker"), u"bookmark"_s, {}, 0, always, [this] {
             m_editor.previousMarker();
             return true;
         }});
    add({u"randomTransitions"_s, tr("Random transitions"), u"shuffle"_s, {}, 0, always,
         [inspector] { return inspector->randomTransitions(); }});
    add({u"removeTransitions"_s, tr("Remove all transitions"), u"delete_sweep"_s, {}, 0, always,
         [inspector] { return inspector->removeAllTransitions(); }});
    const std::pair<int, QString> formats[] = {{0, tr("Format 16:9 (YouTube)")},
                                               {1, tr("Format 9:16 (TikTok, Reels, Shorts)")},
                                               {2, tr("Format 1:1 (square)")},
                                               {3, tr("Format 4:5 (Instagram post)")}};
    for (const auto &[preset, text] : formats) {
        add({u"format%1"_s.arg(preset), text, u"aspect_ratio"_s, {}, 0, always, [this, preset] {
                 m_editor.setCanvasPreset(preset);
                 return true;
             }});
    }

    for (const auto signal : {&EditorController::selectionChanged, &EditorController::modelChanged,
                              &EditorController::undoChanged, &EditorController::splitAvailableChanged}) {
        connect(&editor, signal, this, &ActionRegistry::changed);
    }
    connect(inspector, &ClipInspector::clipboardChanged, this, &ActionRegistry::changed);
}

void ActionRegistry::add(Action action)
{
    m_actions.push_back(std::move(action));
}

void ActionRegistry::setMusicLibrary(AudioLibraryModel *library)
{
    m_music = library;
}

const ActionRegistry::Action *ActionRegistry::find(const QString &id) const
{
    const auto it = std::find_if(m_actions.begin(), m_actions.end(), [&id](const Action &a) { return a.id == id; });
    return it == m_actions.end() ? nullptr : &*it;
}

int ActionRegistry::currentContext() const
{
    if (!m_editor.focusClip()) {
        return m_editor.focusTransition() ? TransitionSelected : Nothing;
    }
    switch (m_editor.inspector()->kind()) {
    case ClipInspector::Video:
        return Video;
    case ClipInspector::Image:
        return Image;
    case ClipInspector::Audio:
        return Audio;
    case ClipInspector::Text:
        return Text;
    default:
        return Image; // other visual clips (colour, compound…): what applies to pictures
    }
}

QVariantList ActionRegistry::toolbar() const
{
    const int context = currentContext();
    QVariantList list;
    for (const Action &action : m_actions) {
        if (action.contexts & context) {
            list << QVariantMap{{u"id"_s, action.id},
                                {u"text"_s, action.text},
                                {u"icon"_s, action.icon},
                                {u"shortcut"_s, action.shortcut},
                                {u"enabled"_s, action.enabled()}};
        }
    }
    return list;
}

bool ActionRegistry::isEnabled(const QString &id) const
{
    const Action *action = find(id);
    return action && action->enabled();
}

bool ActionRegistry::trigger(const QString &id)
{
    const Action *action = find(id);
    return action && action->enabled() && action->run();
}

QVariantList ActionRegistry::search(const QString &text, int limit) const
{
    struct Result
    {
        int score;
        int order;
        QVariantMap value;
    };
    std::vector<Result> results;
    const QString needle = folded(text.trimmed());
    const auto consider = [&](int points, const QString &kind, const QString &id, const QString &name,
                              const QString &detail, const QString &icon, bool enabled) {
        if (points > 0) {
            results.push_back(Result{points, static_cast<int>(results.size()),
                                     QVariantMap{{u"kind"_s, kind},
                                                 {u"id"_s, id},
                                                 {u"text"_s, name},
                                                 {u"detail"_s, detail},
                                                 {u"icon"_s, icon},
                                                 {u"enabled"_s, enabled}}});
        }
    };
    if (needle.isEmpty()) {
        // Nothing typed yet: what can be done with the selection.
        const int context = currentContext();
        for (const Action &action : m_actions) {
            if (action.contexts & context) {
                consider(1, u"action"_s, action.id, action.text, action.shortcut, action.icon, action.enabled());
            }
        }
        QVariantList list;
        for (const Result &result : results) {
            list << result.value;
        }
        return list;
    }
    // Commands first at equal score: they are what a search by name most often means.
    for (const Action &action : m_actions) {
        const int points = score(needle, {action.text, action.id});
        consider(points > 0 ? points * 10 + 5 : 0, u"action"_s, action.id, action.text, action.shortcut,
                 action.icon, action.enabled());
    }
    const fx::Library &library = fx::Library::core();
    for (const fx::FilterPreset &preset : library.filters()) {
        consider(score(needle, {preset.name.en, preset.name.it, tr("Filter")}) * 10, u"filter"_s, preset.id,
                 preset.name.text(), tr("Filter"), u"filter_vintage"_s, true);
    }
    for (const fx::TransitionPreset &preset : library.transitions()) {
        consider(score(needle, {preset.name.en, preset.name.it, tr("Transition")}) * 10, u"transition"_s, preset.id,
                 preset.name.text(), tr("Transition"), u"transition_fade"_s, true);
    }
    for (const fx::TextStylePreset &preset : library.textStyles()) {
        consider(score(needle, {preset.name.en, preset.name.it, tr("Text")}) * 10, u"text"_s, preset.id,
                 preset.name.text(), tr("Text style"), u"title"_s, true);
    }
    for (const fx::AnimationPreset &preset : library.animations()) {
        const QString detail = preset.category == u"in"_s    ? tr("Entry animation")
                               : preset.category == u"out"_s ? tr("Exit animation")
                                                             : tr("Loop animation");
        consider(score(needle, {preset.name.en, preset.name.it, tr("Animation")}) * 10, u"animation"_s, preset.id,
                 preset.name.text(), detail, u"animation"_s, m_editor.clipForLibrary().has_value());
    }
    if (m_music) {
        for (int row = 0; row < m_music->rowCount(); ++row) {
            const QModelIndex index = m_music->index(row);
            const QString name = index.data(AudioLibraryModel::NameRole).toString();
            consider(score(needle, {name, tr("Music")}) * 10, u"music"_s, QString::number(row), name, tr("Music"),
                     u"music_note"_s, index.data(AudioLibraryModel::ReadyRole).toBool());
        }
    }
    for (const Media &media : m_editor.data().media) {
        consider(score(needle, {media.name}) * 10, u"media"_s, media.id.toString(), media.name, tr("Your media"),
                 u"video_library"_s, true);
    }
    std::stable_sort(results.begin(), results.end(), [](const Result &a, const Result &b) { return a.score > b.score; });
    QVariantList list;
    for (const Result &result : results) {
        if (list.size() >= limit) {
            break;
        }
        list << result.value;
    }
    return list;
}

bool ActionRegistry::activate(const QString &kind, const QString &id)
{
    ClipInspector *inspector = m_editor.inspector();
    if (kind == u"action"_s) {
        return trigger(id);
    }
    if (kind == u"filter"_s) {
        emit m_editor.libraryRequested(u"filters"_s);
        return inspector->toggleFilter(id);
    }
    if (kind == u"transition"_s) {
        emit m_editor.libraryRequested(u"transitions"_s);
        return inspector->toggleTransition(id);
    }
    if (kind == u"text"_s) {
        emit m_editor.libraryRequested(u"text"_s);
        return inspector->kind() == ClipInspector::Text ? inspector->applyTextStyle(id) : m_editor.addText(id);
    }
    if (kind == u"animation"_s) {
        emit m_editor.libraryRequested(u"animations"_s);
        return inspector->toggleAnimation(id);
    }
    if (kind == u"music"_s) {
        return m_music && m_editor.addFromLibrary(m_music, id.toInt());
    }
    if (kind == u"media"_s) {
        return m_editor.addMedia(id);
    }
    return false;
}

} // namespace vedit::ui
