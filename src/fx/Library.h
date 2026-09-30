// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Color.h"
#include "fx/Transition.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace vedit::fx {

// A name in the languages of the interface (manifests are data: no qsTr).
struct LocalizedText
{
    QString en;
    QString it;
    QString text() const; // in the language of the interface
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
    QString sampleText;
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

// A sticker of the library (docs/EFFECT_FORMAT.md §6): a picture of the pack (`path`, resolved in the pack's folder),
// an emoji drawn with the system's colour emoji font, or an audio visualizer (`visualizer`: its settings).
struct StickerPreset
{
    QString id;
    int version = 1;
    QString category;
    LocalizedText name;
    QString path; // absolute (":/vedit/packs/…" for the built-in pack); empty for emoji and visualizers
    QString emoji;
    bool animated = false;
    double defaultDuration = 3.0;
    QJsonObject visualizer;
};

// The assets of a pack (docs/EFFECT_FORMAT.md): the core pack is built into the application (:/vedit/packs/…).
class Library
{
public:
    static constexpr const char *kCorePack = "vedit.core";

    // The core pack, loaded once (thread-safe).
    static const Library &core();
    // Loads a pack folder (for tests and, later, user packs). Problems end up in errors().
    static Library load(const QString &folder);

    QString packId() const { return m_packId; }
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
    const std::vector<Category> &animationCategories() const { return m_animationCategories; }
    const std::vector<AnimationPreset> &animations() const { return m_animations; }
    const AnimationPreset *animation(const QString &id) const;
    const std::vector<Category> &stickerCategories() const { return m_stickerCategories; }
    const std::vector<StickerPreset> &stickers() const { return m_stickers; }
    const StickerPreset *sticker(const QString &id) const;
    const std::vector<Category> &videoEffectCategories() const { return m_videoEffectCategories; }
    const std::vector<VideoEffectPreset> &videoEffects() const { return m_videoEffects; }
    const VideoEffectPreset *videoEffect(const QString &id) const;
    const EffectSpec *effect(const QString &id) const;

    // The "look" JSON of a filter manifest / the params of a "vedit.adjust.basic" effect, as colour adjustments.
    static ColorAdjust adjustFromJson(const QJsonObject &look);

private:
    QString m_packId;
    QStringList m_errors;
    std::vector<Category> m_filterCategories;
    std::vector<FilterPreset> m_filters;
    std::vector<Category> m_transitionCategories;
    std::vector<TransitionPreset> m_transitions;
    std::vector<Category> m_textCategories;
    std::vector<TextStylePreset> m_textStyles;
    std::vector<Category> m_animationCategories;
    std::vector<AnimationPreset> m_animations;
    std::vector<Category> m_stickerCategories;
    std::vector<StickerPreset> m_stickers;
    std::vector<Category> m_videoEffectCategories;
    std::vector<VideoEffectPreset> m_videoEffects;
    std::vector<EffectSpec> m_effects;
};

} // namespace vedit::fx
