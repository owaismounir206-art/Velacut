// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Color.h"
#include "fx/Transition.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace velacut::fx {

// A name in the languages of the interface (manifests are data: no qsTr).
struct LocalizedText
{
    QString en;
    QString it;
    QString text() const; // in the language of the interface
    // {"en": …, "it": …}, or one string for both languages.
    static LocalizedText fromJson(const QJsonValue &value);
};

struct Category
{
    QString id;
    LocalizedText name;
};

// A filter: a colour look plus the spatial touches that go with it.
struct FilterPreset
{
    QString id; // "filters/warm"
    int version = 1;
    QString category;
    LocalizedText name;
    ColorAdjust look;
    double vignette = 0;  // −1…+1
    double grain = 0;     // 0…1
    double sharpness = 0; // 0…1
};

struct TransitionPreset
{
    QString id; // "transitions/dissolve"
    int version = 1;
    QString category;
    LocalizedText name;
    TransitionKind kernel = TransitionKind::Dissolve;
    double defaultSeconds = 0.5;
    QJsonObject params; // default parameters ("easing", "softness")
};

struct TextStylePreset
{
    QString id; // "text/outline"
    int version = 1;
    QString category;
    LocalizedText name;
    QJsonObject style; // FILE_FORMAT §5.5 text "style"
    QJsonObject animation;
    LocalizedText sampleText; // what a new text in this style says (templates); empty: "Your text"
};

// A look for captions (FILE_FORMAT §5.5 "captionStyle"): text style, highlight of the word being said, animation.
struct CaptionStylePreset
{
    QString id; // "captions/karaoke-yellow"
    int version = 1;
    QString category;
    LocalizedText name;
    QJsonObject style; // a "captionStyle" object
};

struct AnimationPreset
{
    QString id; // "animations/in/fade"
    int version = 1;
    QString category; // "in", "out", "loop"
    LocalizedText name;
    double defaultSeconds = 0.5;
    QString easing = QStringLiteral("easeInOut");
    QJsonObject params;
};

struct ParamSpec
{
    QString name;
    double min = 0;
    double max = 1;
    double defaultValue = 0;
    LocalizedText label;
    bool advanced = false;
};

struct EffectSpec
{
    QString id; // "vedit.adjust.basic"
    int version = 1;
    LocalizedText name;
    std::vector<ParamSpec> params;
};

// A video effect of the library (docs/EFFECT_FORMAT.md §9): a CPU kernel (`kernel`, fx::EffectKernel's name) with its
// parameters, or an effect of another type (`type`, e.g. "vedit.beat.flash"). `controls`: the parameters shown in the
// properties panel.
struct VideoEffectPreset
{
    QString id;
    int version = 1;
    QString category;
    LocalizedText name;
    QString type; // "vedit.effect" for a kernel
    QString kernel;
    QJsonObject params;
    QStringList controls;
};

// A project template of the library (docs/EFFECT_FORMAT.md §10): its slots, texts, stickers and look, kept as the
// manifest's JSON (the editor builds the timeline from it).
struct TemplatePreset
{
    QString id;
    int version = 1;
    QString category;
    LocalizedText name;
    QJsonObject spec;
};

// A sticker of the library (docs/EFFECT_FORMAT.md §6): a picture of the pack (`path`, resolved in the pack's folder),
// an emoji drawn with the system's colour emoji font, an audio visualizer (`visualizer`: its settings) or an animated
// graphic element (`graphic`).
struct StickerPreset
{
    QString id;
    int version = 1;
    QString category;
    LocalizedText name;
    QString path; // absolute (":/velacut/packs/…" for the built-in pack); empty for emoji and visualizers
    QString emoji;
    bool animated = false;
    double defaultDuration = 3.0;
    QJsonObject visualizer;
    QJsonObject graphic; // an animated graphic element (FILE_FORMAT §5.5)
};

// The assets of a pack (docs/EFFECT_FORMAT.md): the core pack is built into the application (:/velacut/packs/…).
class Library
{
public:
    static constexpr const char *kCorePack = "vedit.core";

    // The library in use: the core pack and, after it, the user's packs (userPacksFolder()). Thread-safe; a reference
    // stays valid for the whole run, also after reload() (old libraries are kept: installing packs is rare).
    static const Library &core();
    // Loads the packs again (a pack was installed or removed): later calls of core() see the new library.
    static void reload();
    // Where the user's packs live: <XDG data>/velacut/packs/<id>/ (docs/EFFECT_FORMAT.md §1).
    static QString userPacksFolder();
    // Loads a pack folder. Only pack.json is required; problems end up in errors().
    static Library load(const QString &folder);
    // Adds the items of `other` (a user pack) whose ids are not taken yet, and its new categories.
    void merge(const Library &other);

    QString packId() const { return m_packId; }
    const LocalizedText &packName() const { return m_packName; }
    int packVersion() const { return m_packVersion; }
    // Number of items of every kind (shown in Preferences → Packs).
    int itemCount() const;
    const QStringList &errors() const { return m_errors; }

    const std::vector<Category> &filterCategories() const { return m_filterCategories; }
    const std::vector<FilterPreset> &filters() const { return m_filters; }
    const FilterPreset *filter(const QString &id) const;
    const std::vector<Category> &transitionCategories() const { return m_transitionCategories; }
    const std::vector<TransitionPreset> &transitions() const { return m_transitions; }
    const TransitionPreset *transition(const QString &id) const;
    const std::vector<Category> &textStyleCategories() const { return m_textCategories; }
    const std::vector<TextStylePreset> &textStyles() const { return m_textStyles; }
    const TextStylePreset *textStyle(const QString &id) const;
    const std::vector<Category> &captionStyleCategories() const { return m_captionCategories; }
    const std::vector<CaptionStylePreset> &captionStyles() const { return m_captionStyles; }
    const CaptionStylePreset *captionStyle(const QString &id) const;
    const std::vector<Category> &animationCategories() const { return m_animationCategories; }
    const std::vector<AnimationPreset> &animations() const { return m_animations; }
    const AnimationPreset *animation(const QString &id) const;
    const std::vector<Category> &stickerCategories() const { return m_stickerCategories; }
    const std::vector<StickerPreset> &stickers() const { return m_stickers; }
    const StickerPreset *sticker(const QString &id) const;
    const std::vector<Category> &videoEffectCategories() const { return m_videoEffectCategories; }
    const std::vector<VideoEffectPreset> &videoEffects() const { return m_videoEffects; }
    const VideoEffectPreset *videoEffect(const QString &id) const;
    const std::vector<Category> &templateCategories() const { return m_templateCategories; }
    const std::vector<TemplatePreset> &templates() const { return m_templates; }
    const TemplatePreset *templatePreset(const QString &id) const;
    const EffectSpec *effect(const QString &id) const;

    // The "look" JSON of a filter manifest / the params of a "vedit.adjust.basic" effect, as colour adjustments.
    static ColorAdjust adjustFromJson(const QJsonObject &look);

private:
    QString m_packId;
    LocalizedText m_packName;
    int m_packVersion = 1;
    QStringList m_errors;
    std::vector<Category> m_filterCategories;
    std::vector<FilterPreset> m_filters;
    std::vector<Category> m_transitionCategories;
    std::vector<TransitionPreset> m_transitions;
    std::vector<Category> m_textCategories;
    std::vector<TextStylePreset> m_textStyles;
    std::vector<Category> m_captionCategories;
    std::vector<CaptionStylePreset> m_captionStyles;
    std::vector<Category> m_animationCategories;
    std::vector<AnimationPreset> m_animations;
    std::vector<Category> m_stickerCategories;
    std::vector<StickerPreset> m_stickers;
    std::vector<Category> m_videoEffectCategories;
    std::vector<VideoEffectPreset> m_videoEffects;
    std::vector<Category> m_templateCategories;
    std::vector<TemplatePreset> m_templates;
    std::vector<EffectSpec> m_effects;
};

} // namespace velacut::fx
