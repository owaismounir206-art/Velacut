// SPDX-License-Identifier: GPL-3.0-or-later
#include "TemplateBuilder.h"

#include "core/project/Clip.h"

#include <QJsonArray>
#include <QLocale>

using namespace Qt::StringLiterals;

namespace vedit {

namespace {

PlaceholderKind placeholderKindFromString(const QString &s)
{
    if (s == u"video"_s) {
        return PlaceholderKind::Video;
    }
    if (s == u"photo"_s) {
        return PlaceholderKind::Photo;
    }
    return PlaceholderKind::Any;
}

QString localizedText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    const QJsonObject obj = value.toObject();
    // Prefer Italian if available and the system is Italian, else English
    const QString lang = QLocale().language() == QLocale::Italian ? u"it"_s : u"en"_s;
    return obj.value(lang).toString(obj.value(u"en"_s).toString());
}

Canvas canvasFromPreset(const QString &preset)
{
    Canvas canvas;
    if (preset == u"16:9"_s) {
        canvas = {1920, 1080, CanvasPreset::Landscape16x9};
    } else if (preset == u"9:16"_s) {
        canvas = {1080, 1920, CanvasPreset::Portrait9x16};
    } else if (preset == u"1:1"_s) {
        canvas = {1080, 1080, CanvasPreset::Square1x1};
    } else if (preset == u"4:5"_s) {
        canvas = {1080, 1350, CanvasPreset::Portrait4x5};
    } else if (preset == u"21:9"_s) {
        canvas = {2560, 1080, CanvasPreset::Cinema21x9};
    } else if (preset == u"3:4"_s) {
        canvas = {1080, 1440, CanvasPreset::Portrait3x4};
    } else {
        canvas = {1920, 1080, CanvasPreset::Custom};
    }
    return canvas;
}

} // namespace

std::optional<Sequence> TemplateBuilder::fromTemplate(const QJsonObject &spec, const Rational &projectRate)
{
    if (spec.value(u"kind"_s).toString() != u"template"_s) {
        return std::nullopt;
    }

    Sequence sequence;
    sequence.id = SequenceId::create();
    sequence.name = localizedText(spec.value(u"name"_s));

    // Canvas from the template
    const QString canvasPreset = spec.value(u"canvas"_s).toString(u"16:9"_s);
    sequence.canvas = canvasFromPreset(canvasPreset);

    // Main track
    Track mainTrack;
    mainTrack.id = TrackId::create();
    mainTrack.kind = TrackKind::Video;

    // Build clips from slots
    const QJsonArray slotArray = spec.value(u"slots"_s).toArray();
    RationalTime position(0, projectRate);

    for (const QJsonValue &slotValue : slotArray) {
        const QJsonObject slot = slotValue.toObject();

        Clip clip;
        clip.id = ClipId::create();
        clip.start = position;

        // Duration
        const double seconds = slot.value(u"seconds"_s).toDouble(3.0);
        const int secondsInt = static_cast<int>(seconds * 100); // 100ths of second for precision
        clip.duration = RationalTime::fromSeconds(Rational(secondsInt, 100), projectRate, Rounding::NearestEven);

        // Placeholder
        Placeholder placeholder;
        placeholder.label = localizedText(slot.value(u"label"_s));
        placeholder.kind = placeholderKindFromString(slot.value(u"kind"_s).toString(u"any"_s));
        clip.placeholder = placeholder;

        // The placeholder is a grey color clip
        clip.payload = ColorClipData{Param(Color{0x5f, 0x63, 0x68, 255})};

        mainTrack.clips.push_back(std::move(clip));
        position = position + clip.duration;
    }

    sequence.visualTracks.push_back(std::move(mainTrack));

    // TODO: Add texts, stickers, transitions, filters from the template spec
    // For now we create the basic structure with placeholders

    return sequence;
}

} // namespace vedit
