// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/EditCommand.h"
#include "core/project/Clip.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace vedit::ui {

class EditorController;

// The properties of the selected clip for the panel on the right (SPEC §4, 0bis rules 5 and 8): values by key, so a
// control binds to `values["opacity"]` and writes with set("opacity", v). A slider drag is one undo step (set() calls
// merge until endGesture()). Changes apply to every selected clip that has the property; the values shown are those
// of the focused clip (the last one clicked).
//
// Keys, by section:
//   video       x, y (canvas fractions from the centre), scale (1 = fitted), rotation (degrees), opacity (0…1),
//               flipH, flipV, fit (0 = whole picture, 1 = fill the canvas)
//   background  background.type (0 = colour, 1 = blurred clip), background.color, background.blur (0…1)
//   audio       volume (dB), fadeIn, fadeOut (seconds)
//   speed       speed (0.1…100), reversed, preservePitch
//   filter      filter (asset id, "" = none), filter.intensity (0…1)
//   adjust      adjust.<name> (the parameters of "vedit.adjust.basic", see adjustParams)
//   keyframes   kf.position, kf.scale, kf.rotation, kf.opacity: 0 = not animated, 1 = animated, 2 = a keyframe at the
//               playhead; kf.available (the playhead is on the clip); kf.easing (of the keyframes at the playhead:
//               "linear", "hold" or an easing preset name). An animated parameter changed with set() gets a keyframe
//               at the playhead (the values shown are those at the playhead).
//   animation   animation.in / .out / .loop (asset id, "" = none), animation.<kind>.name, animation.<kind>.duration
//               (seconds)
//   transition  transition.type (asset id), transition.name, transition.duration, transition.maxDuration (seconds)
//               — when a transition is selected on the timeline instead of clips
//   text        text.content, text.font, text.size (fraction of the canvas height), text.color, text.bold,
//               text.italic, text.underline, text.align (0 left, 1 centre, 2 right), text.stroke, text.strokeColor,
//               text.strokeWidth, text.shadow, text.background, text.backgroundColor, text.letterSpacing,
//               text.lineHeight, text.preset (asset id)
class ClipInspector : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Inspector)
    QML_UNCREATABLE("Provided by Editor.inspector")

    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(QString clipId READ clipId NOTIFY changed FINAL)
    Q_PROPERTY(int kind READ kind NOTIFY changed FINAL)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed FINAL)
    Q_PROPERTY(QStringList sections READ sections NOTIFY changed FINAL)
    Q_PROPERTY(QStringList modifiedSections READ modifiedSections NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed FINAL)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList adjustParams READ adjustParams CONSTANT FINAL)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged FINAL)
    // Where the focused clip is on the canvas, for the handles on the preview: {x, y (centre), width, height,
    // rotation} in canvas pixels, and {start, end} in frames; empty for sounds and transitions.
    Q_PROPERTY(QVariantMap canvasBox READ canvasBox NOTIFY changed FINAL)
    // Frames (from the clip's start) of the keyframes of the focused clip, for the diamonds on the timeline.
    Q_PROPERTY(QVariantList keyframes READ keyframes NOTIFY changed FINAL)
    // Colours offered for texts and backgrounds (content colours, not the theme's).
    Q_PROPERTY(QVariantList swatches READ swatches CONSTANT FINAL)

public:
    enum Kind
    {
        None,
        Video,
        Image,
        Audio,
        Text,
        Other,
        Transition,
    };
    Q_ENUM(Kind)

    explicit ClipInspector(EditorController &editor);

    bool active() const;
    QString clipId() const;
    int kind() const;
    int selectedCount() const;
    QStringList sections() const;
    QStringList modifiedSections() const;
    QVariantMap values() const;
    double durationSeconds() const;
    // {name, label, min, max, default, advanced} for every adjustment, in the order of the manifest.
    QVariantList adjustParams() const;
    bool canPaste() const { return m_clipboard.has_value(); }
    QVariantList swatches() const;
    QVariantMap canvasBox() const;
    QVariantList keyframes() const;

    Q_INVOKABLE bool set(const QString &key, const QVariant &value);
    // Keyframes (SPEC §5.6) of "position", "scale", "rotation" or "opacity" at the playhead: added (with the value shown)
    // or removed; the last one removed leaves that value. Easing of the movement from the keyframes at the playhead to
    // the next ones: "linear", "hold" or a preset name ("easeInOut"…). Jumping: -1 previous, +1 next keyframe.
    Q_INVOKABLE bool toggleKeyframe(const QString &key);
    Q_INVOKABLE bool setKeyframeEasing(const QString &easing);
    Q_INVOKABLE void jumpKeyframe(int direction);
    // The end of a gesture (slider released): the next set() is a new undo step.
    Q_INVOKABLE void endGesture();
    // Back to the defaults of a section ("Ripristina", SPEC 0bis rule 8).
    Q_INVOKABLE bool reset(const QString &section);
    // The section of the focused clip copied to every clip of the same kind ("Applica a tutte", rule 5). For the
    // background: it becomes the default of the video, and every clip of the main track uses it.
    Q_INVOKABLE bool applyToAll(const QString &section);

    // Filters and text styles of the libraries: hover = preview in the player only, click = apply, click on the one
    // applied = remove (SPEC 0bis rule 5).
    Q_INVOKABLE void previewFilter(const QString &filterId);
    Q_INVOKABLE bool toggleFilter(const QString &filterId);
    Q_INVOKABLE void previewTextStyle(const QString &styleId);
    Q_INVOKABLE bool applyTextStyle(const QString &styleId);
    // Transitions of the library, for the cut EditorController::transitionTarget() chooses: the preview returns the
    // frames it covers {start, end} (the interface loops over them); a click adds it (or replaces the one there, keeping
    // its duration), a click on the same one removes it; the new transition becomes the selection.
    Q_INVOKABLE QVariantMap previewTransition(const QString &typeId);
    Q_INVOKABLE bool toggleTransition(const QString &typeId);
    Q_INVOKABLE bool removeTransition();
    // Every cut of the track of the selected transition (else of the main track): none / a random one each
    // (SPEC §5.11bis "rimuovi tutte", "transizione casuale"). One undo step.
    Q_INVOKABLE bool removeAllTransitions();
    Q_INVOKABLE bool randomTransitions();
    // Preset animations of the library (SPEC §5.6) on the clip on screen: the preview returns the frames that show it
    // {start, end}; a click adds it (replacing the one of the same kind), a click on the one applied removes it.
    Q_INVOKABLE QVariantMap previewAnimation(const QString &animationId);
    Q_INVOKABLE bool toggleAnimation(const QString &animationId);
    Q_INVOKABLE void clearPreview();

    // Copy/paste attributes: look, placement, background, volume and text style (not the speed: it changes the length).
    Q_INVOKABLE void copyAttributes();
    Q_INVOKABLE bool pasteAttributes();

    // "Migliora automaticamente": light and colour (adjustments) and volume, computed from the clip (rule 9).
    Q_INVOKABLE bool autoEnhance();

    // The style of a new text ("text/outline": readable on any picture, SPEC 0bis rule 6).
    static TextStyle defaultTextStyle();

signals:
    void changed();
    void clipboardChanged();

private:
    const Clip *focus() const;
    // The keyframe time of the playhead on `clip` (none when the playhead is not on it).
    std::optional<RationalTime> playheadKeyTime(const Clip &clip) const;
    // Whether a move of the playhead changes what the inspector shows (keyframed values, kf.available).
    bool playheadMatters();
    // The clip a library item applies to: the focused one, else the one under the playhead (not selected).
    const Clip *libraryClip() const;
    const vedit::Transition *focusTransition(const Track **track = nullptr) const;
    // The track "all the transitions" act on.
    const Track *transitionTrack() const;
    std::optional<vedit::Transition> plannedTransition(const QString &typeId, const Track **track) const;
    // The selected clips that have `section`.
    std::vector<ClipId> targets(const QString &section) const;
    bool supports(const Clip &clip, const QString &section) const;
    bool update(const std::vector<ClipId> &clips, const std::function<void(Clip &)> &change, const QString &text,
                const QString &mergeTarget);
    MergeKey gestureKey(const QString &target);
    QString sectionOf(const QString &key) const;

    EditorController &m_editor;
    quint64 m_gesture = 1;
    std::optional<Clip> m_clipboard;
    bool m_playheadOnClip = false;
};

} // namespace vedit::ui
