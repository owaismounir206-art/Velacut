// SPDX-License-Identifier: GPL-3.0-or-later
#include "Library.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QStandardPaths>

#include <atomic>
#include <memory>
#include <mutex>

using namespace Qt::StringLiterals;

namespace vedit::fx {

namespace {

LocalizedText localized(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {object.value(u"en"_s).toString(), object.value(u"it"_s).toString()};
}

// A template's text: {"en", "it"}, or one string for both languages (a name, a number…).
LocalizedText sampleTextOf(const QJsonValue &value)
{
    if (value.isString()) {
        return {value.toString(), value.toString()};
    }
    return localized(value);
}

} // namespace

LocalizedText LocalizedText::fromJson(const QJsonValue &value)
{
    return sampleTextOf(value);
}

namespace {

std::vector<Category> categories(const QJsonObject &root)
{
    std::vector<Category> result;
    for (const QJsonValue &value : root.value(u"categories"_s).toArray()) {
        result.push_back({value.toObject().value(u"id"_s).toString(), localized(value.toObject().value(u"name"_s))});
    }
    return result;
}

std::array<double, 3> triple(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    return {array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble()};
}

} // namespace

QString LocalizedText::text() const
{
    // The language of the interface is the system one (main.cpp loads the matching translation).
    if (QLocale().language() == QLocale::Italian && !it.isEmpty()) {
        return it;
    }
    return en.isEmpty() ? it : en;
}

ColorAdjust Library::adjustFromJson(const QJsonObject &look)
{
    ColorAdjust a;
    a.exposure = look.value(u"exposure"_s).toDouble();
    a.brightness = look.value(u"brightness"_s).toDouble();
    a.contrast = look.value(u"contrast"_s).toDouble();
    a.highlights = look.value(u"highlights"_s).toDouble();
    a.shadows = look.value(u"shadows"_s).toDouble();
    a.whites = look.value(u"whites"_s).toDouble();
    a.blacks = look.value(u"blacks"_s).toDouble();
    a.saturation = look.value(u"saturation"_s).toDouble();
    a.vibrance = look.value(u"vibrance"_s).toDouble();
    a.temperature = look.value(u"temperature"_s).toDouble();
    a.tint = look.value(u"tint"_s).toDouble();
    a.fade = look.value(u"fade"_s).toDouble();
    if (look.contains(u"shadowTone"_s)) {
        a.shadowTone = triple(look.value(u"shadowTone"_s));
    }
    if (look.contains(u"highlightTone"_s)) {
        a.highlightTone = triple(look.value(u"highlightTone"_s));
    }
    a.splitAmount = look.value(u"splitAmount"_s).toDouble();
    for (const QJsonValue &point : look.value(u"curve"_s).toArray()) {
        const QJsonArray pair = point.toArray();
        if (pair.size() == 2) {
            a.curve.emplace_back(pair[0].toDouble(), pair[1].toDouble());
        }
    }
    return a;
}

Library Library::load(const QString &folder)
{
    Library library;
    const auto read = [&](const QString &name) -> QJsonObject {
        QFile file(folder + u"/"_s + name);
        if (!file.exists() && name != u"pack.json"_s) {
            return {}; // a pack has only the kinds of items it brings
        }
        if (!file.open(QIODevice::ReadOnly)) {
            library.m_errors << u"%1: cannot be read"_s.arg(name);
            return {};
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            library.m_errors << u"%1: %2"_s.arg(name, error.errorString());
        }
        return document.object();
    };
    const QJsonObject pack = read(u"pack.json"_s);
    library.m_packId = pack.value(u"id"_s).toString();
    library.m_packName = localized(pack.value(u"name"_s));
    library.m_packVersion = pack.value(u"version"_s).toInt(1);
    if (pack.value(u"format"_s).toString() != u"vedit.pack"_s || library.m_packId.isEmpty()) {
        library.m_errors << u"pack.json: not a vedit pack (format \"vedit.pack\" and an id are required)"_s;
    }

    const QJsonObject filters = read(u"filters.json"_s);
    library.m_filterCategories = categories(filters);
    for (const QJsonValue &value : filters.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        FilterPreset preset;
        preset.id = item.value(u"id"_s).toString();
        preset.version = item.value(u"version"_s).toInt(1);
        preset.category = item.value(u"category"_s).toString();
        preset.name = localized(item.value(u"name"_s));
        const QJsonObject look = item.value(u"look"_s).toObject();
        preset.look = adjustFromJson(look);
        preset.vignette = look.value(u"vignette"_s).toDouble();
        preset.grain = look.value(u"grain"_s).toDouble();
        preset.sharpness = look.value(u"sharpness"_s).toDouble();
        library.m_filters.push_back(std::move(preset));
    }

    const QJsonObject transitions = read(u"transitions.json"_s);
    library.m_transitionCategories = categories(transitions);
    for (const QJsonValue &value : transitions.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        TransitionPreset preset;
        preset.id = item.value(u"id"_s).toString();
        preset.version = item.value(u"version"_s).toInt(1);
        preset.category = item.value(u"category"_s).toString();
        preset.name = localized(item.value(u"name"_s));
        const QByteArray kernel = item.value(u"kernel"_s).toString().toLatin1();
        const auto kind = transitionKindFromName(std::string_view(kernel.constData(), static_cast<size_t>(kernel.size())));
        if (!kind) {
            library.m_errors << u"%1: unknown kernel '%2'"_s.arg(preset.id, QString::fromLatin1(kernel));
            continue;
        }
        preset.kernel = *kind;
        preset.defaultSeconds = item.value(u"defaultDuration"_s).toDouble(0.5);
        preset.params = item.value(u"params"_s).toObject();
        library.m_transitions.push_back(std::move(preset));
    }

    const QJsonObject texts = read(u"text-styles.json"_s);
    library.m_textCategories = categories(texts);
    for (const QJsonValue &value : texts.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        library.m_textStyles.push_back({item.value(u"id"_s).toString(), item.value(u"version"_s).toInt(1),
                                        item.value(u"category"_s).toString(), localized(item.value(u"name"_s)),
                                        item.value(u"style"_s).toObject(), item.value(u"animation"_s).toObject(),
                                        sampleTextOf(item.value(u"sampleText"_s))});
    }

    const QJsonObject captions = read(u"caption-styles.json"_s);
    library.m_captionCategories = categories(captions);
    for (const QJsonValue &value : captions.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        library.m_captionStyles.push_back({item.value(u"id"_s).toString(), item.value(u"version"_s).toInt(1),
                                           item.value(u"category"_s).toString(), localized(item.value(u"name"_s)),
                                           item.value(u"captionStyle"_s).toObject()});
    }

    const QJsonObject animations = read(u"animations.json"_s);
    library.m_animationCategories = categories(animations);
    for (const QJsonValue &value : animations.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        AnimationPreset preset;
        preset.id = item.value(u"id"_s).toString();
        preset.version = item.value(u"version"_s).toInt(1);
        preset.category = item.value(u"category"_s).toString();
        preset.name = localized(item.value(u"name"_s));
        preset.defaultSeconds = item.value(u"defaultDuration"_s).toDouble(0.5);
        preset.params = item.value(u"params"_s).toObject();
        preset.easing = preset.params.value(u"easing"_s).toString(u"easeInOut"_s);
        library.m_animations.push_back(std::move(preset));
    }

    const QJsonObject effects = read(u"effects.json"_s);
    for (const QJsonValue &value : effects.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        EffectSpec spec;
        spec.id = item.value(u"id"_s).toString();
        spec.version = item.value(u"version"_s).toInt(1);
        spec.name = localized(item.value(u"name"_s));
        for (const QJsonValue &p : item.value(u"params"_s).toArray()) {
            const QJsonObject param = p.toObject();
            spec.params.push_back({param.value(u"name"_s).toString(), param.value(u"min"_s).toDouble(),
                                   param.value(u"max"_s).toDouble(1.0), param.value(u"default"_s).toDouble(),
                                   localized(param.value(u"label"_s)), param.value(u"advanced"_s).toBool()});
        }
        library.m_effects.push_back(std::move(spec));
    }

    const QJsonObject stickers = read(u"stickers.json"_s);
    if (!stickers.isEmpty()) {
        library.m_stickerCategories = categories(stickers);
        for (const QJsonValue &value : stickers.value(u"items"_s).toArray()) {
            const QJsonObject item = value.toObject();
            StickerPreset preset;
            preset.id = item.value(u"id"_s).toString();
            preset.version = item.value(u"version"_s).toInt(1);
            preset.category = item.value(u"category"_s).toString();
            preset.name = localized(item.value(u"name"_s));
            const QString path = item.value(u"path"_s).toString();
            preset.path = path.isEmpty() ? QString() : folder + u"/"_s + path;
            preset.emoji = item.value(u"emoji"_s).toString();
            preset.animated = item.value(u"animated"_s).toBool(false);
            preset.defaultDuration = item.value(u"defaultDuration"_s).toDouble(3.0);
            preset.visualizer = item.value(u"visualizer"_s).toObject();
            preset.graphic = item.value(u"graphic"_s).toObject();
            library.m_stickers.push_back(std::move(preset));
        }
    }
    const QJsonObject videoEffects = read(u"video-effects.json"_s);
    library.m_videoEffectCategories = categories(videoEffects);
    for (const QJsonValue &value : videoEffects.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        VideoEffectPreset preset;
        preset.id = item.value(u"id"_s).toString();
        preset.version = item.value(u"version"_s).toInt(1);
        preset.category = item.value(u"category"_s).toString();
        preset.name = localized(item.value(u"name"_s));
        preset.type = item.value(u"type"_s).toString(u"vedit.effect"_s);
        preset.kernel = item.value(u"kernel"_s).toString();
        preset.params = item.value(u"params"_s).toObject();
        for (const QJsonValue &control : item.value(u"controls"_s).toArray()) {
            preset.controls << control.toString();
        }
        library.m_videoEffects.push_back(std::move(preset));
    }

    const QJsonObject templates = read(u"templates.json"_s);
    library.m_templateCategories = categories(templates);
    for (const QJsonValue &value : templates.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        library.m_templates.push_back({item.value(u"id"_s).toString(), item.value(u"version"_s).toInt(1),
                                       item.value(u"category"_s).toString(), localized(item.value(u"name"_s)), item});
    }
    return library;
}

namespace {

// The library in use. Replaced as a whole by reload(); the previous ones are never freed, so that references taken by
// other threads (rendering, thumbnails) stay valid.
std::atomic<const Library *> &currentLibrary()
{
    static std::atomic<const Library *> current{nullptr};
    return current;
}

const Library *loadInstalled()
{
    // Owned here until the program ends (never freed earlier: see currentLibrary()).
    static std::mutex mutex;
    static std::vector<std::unique_ptr<Library>> loaded;
    auto owned = std::make_unique<Library>(Library::load(u":/vedit/packs/vedit.core"_s));
    Library *library = owned.get();
    {
        std::lock_guard lock(mutex);
        loaded.push_back(std::move(owned));
    }
    const QDir folder(Library::userPacksFolder());
    for (const QString &entry : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const Library pack = Library::load(folder.filePath(entry));
        if (pack.errors().isEmpty() && pack.packId() != QLatin1String(Library::kCorePack)) {
            library->merge(pack);
        }
    }
    return library;
}

} // namespace

const Library &Library::core()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const Library *expected = nullptr;
        currentLibrary().compare_exchange_strong(expected, loadInstalled());
    });
    return *currentLibrary().load();
}

void Library::reload()
{
    core(); // the first load happened
    currentLibrary().store(loadInstalled());
}

QString Library::userPacksFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/vedit/packs"_s;
}

int Library::itemCount() const
{
    return static_cast<int>(m_filters.size() + m_transitions.size() + m_textStyles.size() + m_captionStyles.size() +
                            m_animations.size() +
                            m_stickers.size() + m_videoEffects.size() + m_templates.size());
}

namespace {

template<typename T>
void mergeItems(std::vector<T> &into, const std::vector<T> &from)
{
    for (const T &item : from) {
        if (std::none_of(into.begin(), into.end(), [&item](const T &existing) { return existing.id == item.id; })) {
            into.push_back(item);
        }
    }
}

} // namespace

void Library::merge(const Library &other)
{
    mergeItems(m_filterCategories, other.m_filterCategories);
    mergeItems(m_filters, other.m_filters);
    mergeItems(m_transitionCategories, other.m_transitionCategories);
    mergeItems(m_transitions, other.m_transitions);
    mergeItems(m_textCategories, other.m_textCategories);
    mergeItems(m_textStyles, other.m_textStyles);
    mergeItems(m_captionCategories, other.m_captionCategories);
    mergeItems(m_captionStyles, other.m_captionStyles);
    mergeItems(m_animationCategories, other.m_animationCategories);
    mergeItems(m_animations, other.m_animations);
    mergeItems(m_stickerCategories, other.m_stickerCategories);
    mergeItems(m_stickers, other.m_stickers);
    mergeItems(m_videoEffectCategories, other.m_videoEffectCategories);
    mergeItems(m_videoEffects, other.m_videoEffects);
    mergeItems(m_templateCategories, other.m_templateCategories);
    mergeItems(m_templates, other.m_templates);
    mergeItems(m_effects, other.m_effects);
}

const FilterPreset *Library::filter(const QString &id) const
{
    for (const FilterPreset &preset : m_filters) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const TransitionPreset *Library::transition(const QString &id) const
{
    for (const TransitionPreset &preset : m_transitions) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const TextStylePreset *Library::textStyle(const QString &id) const
{
    for (const TextStylePreset &preset : m_textStyles) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const CaptionStylePreset *Library::captionStyle(const QString &id) const
{
    for (const CaptionStylePreset &preset : m_captionStyles) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const AnimationPreset *Library::animation(const QString &id) const
{
    for (const AnimationPreset &preset : m_animations) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const EffectSpec *Library::effect(const QString &id) const
{
    for (const EffectSpec &spec : m_effects) {
        if (spec.id == id) {
            return &spec;
        }
    }
    return nullptr;
}

const TemplatePreset *Library::templatePreset(const QString &id) const
{
    for (const TemplatePreset &preset : m_templates) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const VideoEffectPreset *Library::videoEffect(const QString &id) const
{
    for (const VideoEffectPreset &preset : m_videoEffects) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const StickerPreset *Library::sticker(const QString &id) const
{
    for (const StickerPreset &preset : m_stickers) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

} // namespace vedit::fx
