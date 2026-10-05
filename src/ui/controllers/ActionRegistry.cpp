// SPDX-License-Identifier: GPL-3.0-or-later
#include "ActionRegistry.h"

#include "AiController.h"
#include "CaptionsController.h"
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
    const int placed = videoLike | Sticker; // pictures placed on the canvas

    // The contextual toolbar, in order (SPEC 0bis rule 3). Features of later phases get their entry with them.
    add({u"split"_s, tr("Split"), u"content_cut"_s, u"S"_s, AnyClip | Nothing, [this] { return m_editor.canSplit(); },
         [this] { return m_editor.split(); }});
    // Q / W: everything of the clip before (after) the playhead goes, the gap closes (SPEC §5.2).
    add({u"rippleTrimLeft"_s, tr("Delete left of the playhead"), u"keyboard_tab_rtl"_s, u"Q"_s, AnyClip | Nothing,
         [this] { return m_editor.canRippleTrimLeft(); }, [this] { return m_editor.rippleTrimLeft(); }});
    add({u"rippleTrimRight"_s, tr("Delete right of the playhead"), u"keyboard_tab"_s, u"W"_s, AnyClip | Nothing,
         [this] { return m_editor.canRippleTrimRight(); }, [this] { return m_editor.rippleTrimRight(); }});
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
    add({u"animation"_s, tr("Animation"), u"animation"_s, {}, placed | Text, clip, library(u"animations"_s)});
    add({u"stickerEdit"_s, tr("Edit sticker"), u"tune"_s, {}, Sticker, clip, properties(u"sticker"_s)});
    add({u"beat"_s, tr("Beat"), u"graphic_eq"_s, {}, Audio, clip, [this] {
             m_editor.detectBeats();
             return true;
         }});
    add({u"freeze"_s, tr("Freeze"), u"ac_unit"_s, {}, Video | Nothing, always, [this] { return m_editor.freezeFrame(); }});
    add({u"reverse"_s, tr("Reverse"), u"swap_horiz"_s, {}, Video, clip, [inspector] {
             return inspector->set(u"reversed"_s, !inspector->values().value(u"reversed"_s).toBool());
         }});
    add({u"mirror"_s, tr("Mirror"), u"flip"_s, {}, placed, clip, [inspector] {
             return inspector->set(u"flipH"_s, !inspector->values().value(u"flipH"_s).toBool());
         }});
    add({u"rotate"_s, tr("Rotate"), u"rotate_right"_s, {}, placed | Text, clip, [inspector] {
             const double rotation = inspector->values().value(u"rotation"_s).toDouble() + 90.0;
             const bool done = inspector->set(u"rotation"_s, rotation > 180.0 ? rotation - 360.0 : rotation);
             inspector->endGesture();
             return done;
         }});
    add({u"enhance"_s, tr("Enhance"), u"auto_fix_high"_s, {}, videoLike | Audio, clip,
         [inspector] { return inspector->autoEnhance(); }});
    // One click, a result made of ordinary cuts (SPEC 0bis rule 9).
    add({u"removePauses"_s, tr("Remove pauses"), u"voice_over_off"_s, {}, Video | Audio,
         [this] { return m_editor.ai()->canRemovePauses(); }, [this] { return m_editor.ai()->removePauses(); }});
    add({u"splitScenes"_s, tr("Split scenes"), u"view_week"_s, {}, Video,
         [this] { return m_editor.ai()->canSplitScenes(); }, [this] { return m_editor.ai()->splitScenes(); }});
    add({u"stabilize"_s, tr("Stabilize"), u"vibration"_s, {}, Video,
         [this] { return m_editor.ai()->canStabilize(); }, [this] { return m_editor.ai()->stabilize(); }});
    // Another video or photo in the same place, with the same length and look (SPEC 0bis rule 3, §5.2, §5.13).
    add({u"replace"_s, tr("Replace"), u"find_replace"_s, {}, videoLike, clip, [this] {
             const std::optional<ClipId> focus = m_editor.focusClip();
             if (!focus) {
                 return false;
             }
             emit m_editor.replaceRequested(focus->toString());
             return true;
         }});
    add({u"adjustLayer"_s, tr("Adjust"), u"tune"_s, {}, AdjustmentLayer, clip, properties(u"adjust"_s)});
    add({u"filterLayer"_s, tr("Filters"), u"filter_vintage"_s, {}, AdjustmentLayer, clip, library(u"filters"_s)});
    add({u"transitionAll"_s, tr("Apply to all cuts"), u"transition_fade"_s, {}, TransitionSelected, always,
         [inspector] { return inspector->applyToAll(u"transition"_s); }});
    add({u"addText"_s, tr("Add text"), u"title"_s, {}, Nothing, always, [this] { return m_editor.addText(); }});
    add({u"addAudio"_s, tr("Add audio"), u"music_note"_s, {}, Nothing, always, library(u"audio"_s)});
    add({u"captions"_s, tr("Captions"), u"subtitles"_s, {}, Nothing, always, library(u"captions"_s)});
    add({u"addSticker"_s, tr("Add a sticker"), u"add_reaction"_s, {}, 0, always, library(u"stickers"_s)});
    add({u"addEffect"_s, tr("Add an effect"), u"auto_awesome"_s, {}, 0, always, library(u"effects"_s)});

    // Found by the search (and, some, in the right-click menu of the timeline).
    add({u"copyAttributes"_s, tr("Copy attributes"), u"format_paint"_s, tr("Ctrl+Alt+C"), 0, clip, [inspector] {
             inspector->copyAttributes();
             return true;
         }});
    add({u"pasteAttributes"_s, tr("Paste attributes"), u"content_paste"_s, tr("Ctrl+Alt+V"), 0,
         [this, inspector] { return inspector->canPaste() && !m_editor.selectedClips().empty(); },
         [inspector] { return inspector->pasteAttributes(); }});
    add({u"rippleDelete"_s, tr("Ripple delete"), u"backspace"_s, tr("Shift+Del"), 0, clip,
         [this] { return m_editor.rippleDeleteSelection(); }});
    add({u"nextCut"_s, tr("Next cut"), u"skip_next"_s, tr("Down"), 0, always, [this] {
             m_editor.nextCut();
             return true;
         }});
    add({u"previousCut"_s, tr("Previous cut"), u"skip_previous"_s, tr("Up"), 0, always, [this] {
             m_editor.previousCut();
             return true;
         }});
    add({u"toggleMagnetic"_s, tr("Toggle magnetic track"), u"auto_awesome_motion"_s, u"N"_s, 0, always, [this] {
             return m_editor.toggleMagneticMain();
         }});
    add({u"toggleSnapping"_s, tr("Toggle snapping"), u"straighten"_s, u"\\"_s, 0, always, [this] {
             m_editor.toggleSnapping();
             return true;
         }});
    add({u"setInPoint"_s, tr("Set In point"), u"first_page"_s, u"I"_s, 0, always, [this] {
             m_editor.setInPoint();
             return true;
         }});
    add({u"setOutPoint"_s, tr("Set Out point"), u"last_page"_s, u"O"_s, 0, always, [this] {
             m_editor.setOutPoint();
             return true;
         }});
    add({u"clearInOut"_s, tr("Clear In/Out points"), u"clear"_s, tr("Alt+X"), 0, [this] { return m_editor.hasInOut(); }, [this] {
             m_editor.clearInOut();
             return true;
         }});
    add({u"createCompound"_s, tr("Group into a compound clip"), u"stacks"_s, {}, 0,
         [this] { return !m_editor.selectedClips().empty(); }, [this] { return m_editor.createCompoundClip(); }});
    add({u"expandCompound"_s, tr("Ungroup the compound clip"), u"stacks"_s, {}, 0,
         [this] {
             const std::optional<ClipId> focus = m_editor.focusClip();
             const Clip *clip = focus ? m_editor.data().findClip(*focus) : nullptr;
             return clip && std::holds_alternative<CompoundClipData>(clip->payload);
         },
         [this] { return m_editor.expandCompoundClip(); }});
    add({u"createMulticam"_s, tr("Create multicam clip"), u"video_settings"_s, {}, 0,
         [this] { return m_editor.selectedClips().size() >= 2; },
         [this] { return m_editor.createMulticamFromSelection(); }});
    add({u"syncAudio"_s, tr("Synchronize audio"), u"sync"_s, {}, 0,
         [this] { return m_editor.selectedClips().size() >= 2; },
         [this] { return m_editor.syncSelectedClipsByAudio(); }});
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
                              &EditorController::undoChanged, &EditorController::splitAvailableChanged,
                              &EditorController::snappingChanged, &EditorController::magneticMainChanged,
                              &EditorController::inOutChanged}) {
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
    case ClipInspector::Adjustment:
        return AdjustmentLayer;
    case ClipInspector::Sticker:
        return Sticker;
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
    for (const fx::CaptionStylePreset &preset : library.captionStyles()) {
        consider(score(needle, {preset.name.en, preset.name.it, tr("Captions")}) * 10, u"captionStyle"_s, preset.id,
                 preset.name.text(), tr("Caption style"), u"subtitles"_s, true);
    }
    for (const fx::VideoEffectPreset &preset : library.videoEffects()) {
        consider(score(needle, {preset.name.en, preset.name.it, tr("Effect")}) * 10, u"effect"_s, preset.id,
                 preset.name.text(), tr("Effect"), u"auto_awesome"_s, true);
    }
    for (const fx::StickerPreset &preset : library.stickers()) {
        consider(score(needle, {preset.name.en, preset.name.it, tr("Sticker")}) * 10, u"sticker"_s, preset.id,
                 preset.name.text(), tr("Sticker"), u"add_reaction"_s, true);
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
    if (kind == u"captionStyle"_s) {
        emit m_editor.libraryRequested(u"captions"_s);
        return m_editor.captions()->applyStyle(id);
    }
    if (kind == u"animation"_s) {
        emit m_editor.libraryRequested(u"animations"_s);
        return inspector->toggleAnimation(id);
    }
    if (kind == u"effect"_s) {
        emit m_editor.libraryRequested(u"effects"_s);
        return inspector->toggleEffect(id);
    }
    if (kind == u"sticker"_s) {
        emit m_editor.libraryRequested(u"stickers"_s);
        return m_editor.addSticker(id);
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
