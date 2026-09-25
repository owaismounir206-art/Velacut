// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineProjection.h"

#include "engine/timeline/ClipPlacement.h"
#include "engine/timeline/MediaProducerCache.h"
#include "fx/Library.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <QLoggingCategory>

#include <mlt++/Mlt.h>

#include <cmath>
#include <numeric>

Q_LOGGING_CATEGORY(lcProjection, "vedit.engine.projection")

using namespace Qt::StringLiterals;

namespace vedit::engine {

namespace {

const Sequence *sequenceOf(const ProjectData &project, const SequenceId &sequenceId)
{
    return project.findSequence(sequenceId);
}

// MLT colour strings with alpha are "#AARRGGBB" (mlt_property.c), our model's are "#RRGGBBAA".
QByteArray mltColor(const Color &color)
{
    return QByteArray("color:") + QString::asprintf("#%02X%02X%02X%02X", color.a, color.r, color.g, color.b).toLatin1();
}

// MLT "hide" flags of a track producer: 1 = video, 2 = audio.
int hideFlags(const Track &track, bool audioTrack, bool anySolo)
{
    int flags = 0;
    if (audioTrack || track.hidden) {
        flags |= 1;
    }
    if (track.muted || (anySolo && !track.solo)) {
        flags |= 2;
    }
    return flags;
}

// The value a static renderer uses for a parameter (keyframes: the first one; animations are rendered from Phase 3).
ParamValue valueOf(const Param &param)
{
    return param.isAnimated() ? param.keyframes().front().value : param.staticValue();
}

double numberOf(const Param &param, double fallback)
{
    const ParamValue value = valueOf(param);
    return std::holds_alternative<double>(value) ? std::get<double>(value) : fallback;
}

Vec2 vectorOf(const Param &param, Vec2 fallback)
{
    const ParamValue value = valueOf(param);
    return std::holds_alternative<Vec2>(value) ? std::get<Vec2>(value) : fallback;
}

// Displayed size of a media item (rotation and pixel aspect applied).
fx::Easing easingOf(const QString &name)
{
    if (name == u"linear"_s) {
        return fx::Easing::Linear;
    }
    if (name == u"easeIn"_s) {
        return fx::Easing::EaseIn;
    }
    if (name == u"easeOut"_s) {
        return fx::Easing::EaseOut;
    }
    return fx::Easing::EaseInOut;
}

QByteArray textKey(const TextClipData &text)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    const TextStyle &s = text.style;
    stream << text.text << s.fontFamily << s.fontWeight << s.italic << numberOf(s.size, 0) << numberOf(s.letterSpacing, 0)
           << s.lineHeight << int(s.align) << s.underline << text.boxWidth.value_or(-1.0);
    const ParamValue color = valueOf(s.color);
    stream << (std::holds_alternative<Color>(color) ? std::get<Color>(color).toString() : QString());
    stream << s.stroke.has_value() << s.shadow.has_value() << s.background.has_value();
    if (s.stroke) {
        const ParamValue strokeColor = valueOf(s.stroke->color);
        stream << (std::holds_alternative<Color>(strokeColor) ? std::get<Color>(strokeColor).toString() : QString())
               << s.stroke->width;
    }
    if (s.shadow) {
        stream << s.shadow->color.toString() << s.shadow->offset.x << s.shadow->offset.y << s.shadow->blur;
    }
    if (s.background) {
        stream << s.background->color.toString() << s.background->padding << s.background->radius;
    }
    return bytes;
}

// Static copy of a text clip's data for the renderer (animated values at their first keyframe).
TextClipData staticText(const TextClipData &text)
{
    TextClipData copy = text;
    copy.style.size = Param(numberOf(text.style.size, 0.06));
    copy.style.color = Param(valueOf(text.style.color));
    copy.style.letterSpacing = Param(numberOf(text.style.letterSpacing, 0.0));
    if (copy.style.stroke) {
        copy.style.stroke->color = Param(valueOf(text.style.stroke->color));
    }
    return copy;
}

} // namespace

VideoFormat sequenceFormat(const ProjectData &project, const SequenceId &sequenceId)
{
    const Sequence *sequence = sequenceOf(project, sequenceId);
    const Canvas canvas = sequence ? sequence->canvas : project.settings.defaultCanvas;
    return VideoFormat{QSize(canvas.width, canvas.height), project.settings.frameRate};
}

std::unique_ptr<Mlt::Profile> makeProfile(const VideoFormat &format)
{
    auto profile = std::make_unique<Mlt::Profile>();
    profile->set_width(format.size.width());
    profile->set_height(format.size.height());
    profile->set_frame_rate(static_cast<int>(format.frameRate.num()), static_cast<int>(format.frameRate.den()));
    profile->set_sample_aspect(1, 1);
    const int divisor = std::gcd(format.size.width(), format.size.height());
    profile->set_display_aspect(format.size.width() / divisor, format.size.height() / divisor);
    profile->set_progressive(1);
    profile->set_colorspace(709);
    profile->set_explicit(1);
    return profile;
}

std::unique_ptr<Mlt::Profile> makeProfile(const ProjectData &project, const SequenceId &sequenceId)
{
    return makeProfile(sequenceFormat(project, sequenceId));
}

bool profileMatches(const Mlt::Profile &profile, const ProjectData &project, const SequenceId &sequenceId)
{
    auto &p = const_cast<Mlt::Profile &>(profile); // Mlt::Profile getters are not const
    const Sequence *sequence = sequenceOf(project, sequenceId);
    const Canvas canvas = sequence ? sequence->canvas : project.settings.defaultCanvas;
    return p.width() == canvas.width && p.height() == canvas.height &&
           p.frame_rate_num() == project.settings.frameRate.num() && p.frame_rate_den() == project.settings.frameRate.den();
}

TimelineProjection::TimelineProjection(Mlt::Profile &profile, MediaProducerCache &cache, MediaLoading loading)
    : m_profile(profile)
    , m_rate(profile.frame_rate_num(), profile.frame_rate_den())
    , m_cache(cache)
    , m_loading(loading)
{
}

TimelineProjection::~TimelineProjection()
{
    m_tractor.reset();
    m_tracks.clear();
    m_masterMeter.reset();
    m_retired.clear();
}

std::int64_t TimelineProjection::toFrames(const RationalTime &time) const
{
    // Identity when the profile has the project frame rate (D-04); otherwise the nearest frame.
    return time.rate() == m_rate ? time.value() : time.rescaled(m_rate, Rounding::NearestEven).value();
}

std::shared_ptr<Mlt::Producer> TimelineProjection::producerFor(const Media &media, double speed, bool preservePitch)
{
    return m_loading == MediaLoading::Wait ? m_cache.open(media, speed, preservePitch)
                                           : m_cache.producerOrRequest(media, speed, preservePitch);
}

const Clip &TimelineProjection::previewed(const Clip &clip) const
{
    return m_preview.clip && m_preview.clip->id == clip.id ? *m_preview.clip : clip;
}

std::vector<TimelineProjection::StructureSlot> TimelineProjection::structureOf(const Sequence &sequence)
{
    std::vector<StructureSlot> layout;
    for (const Track &track : sequence.visualTracks) {
        layout.push_back({track.id, SlotKind::Clips});
        layout.push_back({track.id, SlotKind::Transitions});
    }
    for (const Track &track : sequence.audioTracks) {
        layout.push_back({track.id, SlotKind::Clips});
    }
    return layout;
}

void TimelineProjection::build(const ProjectData &project, const SequenceId &sequenceId)
{
    m_sequenceId = sequenceId;
    m_transitions.clear();
    m_tracks.clear();
    m_masterMeter.reset();
    m_warnings.clear();
    m_backgroundLength = 0;
    m_tractor = std::make_unique<Mlt::Tractor>(m_profile);
    m_black = std::make_unique<Mlt::Producer>(m_profile, mltColor(Color{0, 0, 0, 255}).constData());
    m_black->set("length", 0x7fffffff);
    m_background = std::make_unique<Mlt::Playlist>(m_profile);
    m_tractor->set_track(*m_background, 0);
    // Level of the whole mix, for the master meter.
    GainSettings master;
    master.meterKey = "master";
    m_masterMeter = makeGainFilter(m_profile, master);
    m_tractor->attach(*m_masterMeter);

    const Sequence *sequence = sequenceOf(project, sequenceId);
    if (!sequence) {
        m_warnings << u"sequence not found"_s;
        updateBackground();
        return;
    }
    bool anySolo = false;
    for (const Track &track : sequence->audioTracks) {
        anySolo = anySolo || track.solo;
    }
    int index = 1;
    for (const StructureSlot &structure : structureOf(*sequence)) {
        const Track *track = project.findTrack(structure.id);
        TrackSlot slot;
        slot.id = structure.id;
        slot.kind = structure.kind;
        slot.audio = !isVisualTrackKind(track->kind);
        slot.playlist = std::make_unique<Mlt::Playlist>(m_profile);
        fillSlot(slot, *track, project, anySolo);
        m_tractor->set_track(*slot.playlist, index);
        if (!slot.audio) {
            auto composite = std::make_unique<Mlt::Transition>(m_profile, "vedit.composite");
            composite->set("always_active", 1);
            m_tractor->plant_transition(*composite, 0, index);
            m_transitions.push_back(std::move(composite));
        }
        if (slot.kind == SlotKind::Clips) {
            auto mix = std::make_unique<Mlt::Transition>(m_profile, "mix");
            mix->set("always_active", 1);
            mix->set("sum", 1);
            m_tractor->plant_transition(*mix, 0, index);
            m_transitions.push_back(std::move(mix));
        }
        m_tracks.push_back(std::move(slot));
        ++index;
    }
    updateBackground();
}

void TimelineProjection::fillSlot(TrackSlot &slot, const Track &track, const ProjectData &project, bool anySolo)
{
    if (slot.kind == SlotKind::Transitions) {
        // Only pictures: the audio of the two clips plays from the clips track.
        slot.playlist->set("hide", 2 | (track.hidden ? 1 : 0));
        patch(slot, transitionEntries(slot, track, project));
        return;
    }
    slot.playlist->set("hide", hideFlags(track, slot.audio, anySolo));
    updateTrackGain(slot, track);
    patch(slot, clipEntries(slot, track, project));
}

void TimelineProjection::updateTrackGain(TrackSlot &slot, const Track &track)
{
    GainSettings settings;
    settings.gainDb = numberOf(track.gainDb, 0.0);
    settings.meterKey = track.id.toString().toLatin1();
    const QByteArray key = settings.key();
    if (slot.gainFilter && key == slot.gainKey) {
        return;
    }
    if (slot.gainFilter) {
        slot.playlist->detach(*slot.gainFilter);
        Retired retired;
        retired.since = std::chrono::steady_clock::now();
        retired.filters.push_back(std::move(slot.gainFilter));
        m_retired.push_back(std::move(retired));
    }
    slot.gainFilter = makeGainFilter(m_profile, settings);
    slot.playlist->attach(*slot.gainFilter);
    slot.gainKey = key;
}

std::shared_ptr<Mlt::Producer> TimelineProjection::colorProducer(const Color &color)
{
    const QByteArray resource = mltColor(color);
    if (auto existing = m_colors.value(resource)) {
        return existing;
    }
    auto producer = std::make_shared<Mlt::Producer>(m_profile, resource.constData());
    producer->set("length", 0x7fffffff);
    m_colors.insert(resource, producer);
    return producer;
}

std::shared_ptr<Mlt::Producer> TimelineProjection::textProducer(const TextClipData &text)
{
    const QByteArray key = textKey(text);
    if (auto existing = m_texts.value(key)) {
        return existing;
    }
    std::shared_ptr<Mlt::Producer> producer = makeTextProducer(m_profile, staticText(text));
    m_texts.insert(key, producer);
    return producer;
}

std::shared_ptr<const TimelineProjection::ClipRender> TimelineProjection::renderOf(const Clip &clip, const Track &track,
                                                                                const ProjectData &project, const Media *media,
                                                                                bool mainTrack, int in, std::int64_t length)
{
    Q_UNUSED(track);
    auto render = std::make_shared<ClipRender>();
    const bool visual = !(media && media->kind == MediaKind::Audio) &&
                        !(clip.media() && clip.media()->streams == Streams::AudioOnly);
    if (visual) {
        for (const Effect &effect : clip.effects) {
            if (!effect.enabled) {
                continue;
            }
            AdjustSettings adjust;
            adjust.intensity = numberOf(effect.intensity, 1.0);
            if (effect.type == u"vedit.filter"_s) {
                const fx::FilterPreset *preset =
                    effect.preset && effect.preset->pack == QLatin1StringView(fx::Library::kCorePack)
                        ? fx::Library::core().filter(effect.preset->id)
                        : nullptr;
                if (!preset) {
                    m_warnings << u"clip %1: filter %2 is not installed"_s.arg(clip.id.toString(),
                                                                              effect.preset ? effect.preset->id : QString());
                    continue;
                }
                adjust.look = preset->look;
                adjust.vignette = preset->vignette;
                adjust.grain = preset->grain;
                adjust.sharpness = preset->sharpness;
            } else if (effect.type == u"vedit.adjust.basic"_s) {
                QJsonObject look;
                for (const auto &[name, param] : effect.params) {
                    look.insert(name, numberOf(param, 0.0));
                }
                adjust.look = fx::Library::adjustFromJson(look);
                adjust.vignette = look.value(u"vignette"_s).toDouble();
                adjust.grain = look.value(u"grain"_s).toDouble();
                adjust.sharpness = look.value(u"sharpness"_s).toDouble();
            } else if (effect.type == u"vedit.chroma_key"_s) {
                ChromaKeySettings ck;
                for (const auto &[name, param] : effect.params) {
                    if (name == u"keyColor"_s) {
                        if (const auto *c = std::get_if<Color>(&param.staticValue())) {
                            ck.keyColor = *c;
                        }
                    } else if (name == u"similarity"_s) {
                        ck.similarity = numberOf(param, 0.4);
                    } else if (name == u"smoothness"_s) {
                        ck.smoothness = numberOf(param, 0.1);
                    } else if (name == u"spill"_s) {
                        ck.spill = numberOf(param, 0.5);
                    }
                }
                render->chromaKey = ck;
                continue;
            } else {
                m_warnings << u"clip %1: effect %2 is rendered from a later phase"_s.arg(clip.id.toString(), effect.type);
                continue;
            }
            render->adjusts.push_back(adjust);
        }
        if (!clip.masks.empty()) {
            MaskSettings maskSettings;
            maskSettings.masks = clip.masks;
            maskSettings.firstFrame = in;
            maskSettings.sourceIn = clip.media() ? clip.media()->sourceIn : RationalTime(0, m_profile.fps());
            maskSettings.frameRate = Rational(m_profile.fps(), 1);
            render->maskSettings = maskSettings;
        }
        TransformSettings transform;
        if (media) {
            const QSizeF source = displaySize(*media);
            transform.sourceWidth = source.width();
            transform.sourceHeight = source.height();
            transform.fit = clip.transform.fit;
        } else {
            // Text and colour clips are canvas-sized layers.
            transform.sourceWidth = m_profile.width();
            transform.sourceHeight = m_profile.height();
            transform.fit = FitMode::Stretch;
        }
        const Vec2 position = vectorOf(clip.transform.position, {0, 0});
        const Vec2 scale = vectorOf(clip.transform.scale, {1, 1});
        transform.x = position.x;
        transform.y = position.y;
        transform.scaleX = scale.x;
        transform.scaleY = clip.transform.uniformScale ? scale.x : scale.y;
        transform.rotation = numberOf(clip.transform.rotation, 0.0);
        transform.flipH = clip.transform.flipH;
        transform.flipV = clip.transform.flipV;
        transform.cropLeft = std::clamp(numberOf(clip.transform.crop.left, 0.0), 0.0, 1.0);
        transform.cropTop = std::clamp(numberOf(clip.transform.crop.top, 0.0), 0.0, 1.0);
        transform.cropRight = std::clamp(numberOf(clip.transform.crop.right, 0.0), 0.0, 1.0);
        transform.cropBottom = std::clamp(numberOf(clip.transform.crop.bottom, 0.0), 0.0, 1.0);
        transform.opacity = std::clamp(numberOf(clip.opacity, 1.0), 0.0, 1.0);
        transform.blendMode = static_cast<vedit::fx::BlendMode>(clip.blendMode);

        transform.positionParam = clip.transform.position;
        transform.scaleParam = clip.transform.scale;
        transform.rotationParam = clip.transform.rotation;
        transform.opacityParam = clip.opacity;
        transform.cropLeftParam = clip.transform.crop.left;
        transform.cropTopParam = clip.transform.crop.top;
        transform.cropRightParam = clip.transform.crop.right;
        transform.cropBottomParam = clip.transform.crop.bottom;
        transform.firstFrame = in;
        transform.sourceIn = clip.media() ? clip.media()->sourceIn : RationalTime(0, m_profile.fps());
        transform.frameRate = Rational(m_profile.fps(), 1);

        if (mainTrack) {
            const Sequence *sequence = sequenceOf(project, m_sequenceId);
            std::optional<CanvasBackground> background = clip.background;
            if (!background && sequence) {
                background = sequence->defaultBackground;
            }
            // Plain black is already the canvas: no work.
            if (background && !(background->type == BackgroundType::Color && background->color == Color{0, 0, 0, 255})) {
                if (background->type == BackgroundType::Image || background->type == BackgroundType::Pattern) {
                    m_warnings << u"clip %1: image and pattern backgrounds are rendered from a later phase"_s.arg(clip.id.toString());
                } else {
                    transform.background = background;
                }
            }
        }
        if (!(media == nullptr && transform.isIdentityLayer())) {
            render->transform = transform;
        }
    }
    if (media && media->info.audio && media->kind != MediaKind::Image) {
        const MediaClipData *data = clip.media();
        GainSettings gain;
        gain.gainDb = numberOf(data->audio.gainDb, 0.0);
        gain.muted = data->audio.muted || data->streams == Streams::VideoOnly;
        gain.pan = std::clamp(numberOf(data->audio.pan, 0.0), -1.0, 1.0);
        gain.fadeInFrames = data->audio.fadeIn ? static_cast<int>(toFrames(*data->audio.fadeIn)) : 0;
        gain.fadeOutFrames = data->audio.fadeOut ? static_cast<int>(toFrames(*data->audio.fadeOut)) : 0;
        gain.length = static_cast<int>(length);
        gain.firstFrame = in;
        if (!gain.isNeutral()) {
            render->gain = gain;
        }
    }
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << render->chromaKey.has_value() << (render->chromaKey ? render->chromaKey->key() : QByteArray());
    stream << render->maskSettings.has_value() << (render->maskSettings ? render->maskSettings->key() : QByteArray());
    for (const AdjustSettings &adjust : render->adjusts) {
        stream << adjust.key();
    }
    stream << render->transform.has_value() << (render->transform ? render->transform->key() : QByteArray());
    stream << render->gain.has_value() << (render->gain ? render->gain->key() : QByteArray());
    render->key = key;
    return render;
}

std::optional<TimelineProjection::Placed> TimelineProjection::place(const Clip &modelClip, const Track &track,
                                                                    const ProjectData &project, bool mainTrack,
                                                                    QSet<MediaId> &usedMedia)
{
    const Clip &clip = previewed(modelClip);
    Placed placed;
    placed.clip = &clip;
    placed.start = toFrames(clip.start);
    placed.length = toFrames(clip.end()) - placed.start;
    if (placed.length <= 0) {
        return std::nullopt; // shorter than one frame at the output rate
    }
    if (!clip.enabled) {
        return placed;
    }
    if (const MediaClipData *data = clip.media()) {
        usedMedia.insert(data->mediaId);
        const Media *media = project.findMedia(data->mediaId);
        if (!media) {
            return placed;
        }
        const bool image = media->kind == MediaKind::Image;
        if (data->curve) {
            m_warnings << u"clip %1: speed curves are rendered from a later phase"_s.arg(clip.id.toString());
        }
        const double speed = image || data->curve ? 1.0 : data->speed;
        const double signedSpeed = data->reversed && !image ? -speed : speed;
        placed.producer = producerFor(*media, signedSpeed, data->preservePitch);
        if (!placed.producer) {
            if (!m_cache.error(media->id).isEmpty()) {
                m_warnings << m_cache.error(media->id);
            }
            return placed;
        }
        const std::int64_t sourceIn = toFrames(data->sourceIn);
        if (image) {
            placed.in = 0;
        } else if (!data->reversed) {
            placed.in = static_cast<int>(std::llround(static_cast<double>(sourceIn) / speed));
        } else {
            // Backwards: the clip plays [sourceIn, sourceIn + length × speed) from its end (phase2_probe: frame n of
            // "timewarp:-s" is source frame length − 1 − n·s).
            const std::int64_t warpedLength = placed.producer->get_length();
            placed.in = static_cast<int>(std::max<std::int64_t>(
                0, warpedLength - std::llround(static_cast<double>(sourceIn) / speed) - placed.length));
        }
        placed.render = renderOf(clip, track, project, media, mainTrack, placed.in, placed.length);
        return placed;
    }
    if (const auto *color = std::get_if<ColorClipData>(&clip.payload)) {
        const ParamValue value = valueOf(color->color);
        placed.producer = colorProducer(std::holds_alternative<Color>(value) ? std::get<Color>(value) : Color{});
        placed.render = renderOf(clip, track, project, nullptr, mainTrack, 0, placed.length);
        return placed;
    }
    if (const TextClipData *text = clip.text()) {
        placed.producer = textProducer(*text);
        placed.render = renderOf(clip, track, project, nullptr, false, 0, placed.length);
        return placed;
    }
    m_warnings << u"clip %1: this kind of clip is rendered from a later phase"_s.arg(clip.id.toString());
    return placed;
}

std::vector<TimelineProjection::Entry> TimelineProjection::clipEntries(TrackSlot &slot, const Track &track,
                                                                       const ProjectData &project)
{
    std::vector<Entry> entries;
    slot.media.clear();
    const Sequence *sequence = sequenceOf(project, m_sequenceId);
    const bool mainTrack = sequence && !sequence->visualTracks.empty() && sequence->visualTracks.front().id == track.id;
    const auto addBlank = [&entries](std::int64_t length) {
        // Adjacent blanks (a gap next to a disabled clip) are one entry.
        if (!entries.empty() && !entries.back().producer) {
            entries.back().out += static_cast<int>(length);
        } else {
            entries.push_back(Entry{nullptr, 0, static_cast<int>(length - 1), nullptr, {}});
        }
    };
    std::int64_t cursor = 0;
    for (const Clip &modelClip : track.clips) {
        std::optional<Placed> placed = place(modelClip, track, project, mainTrack, slot.media);
        if (!placed) {
            continue;
        }
        std::int64_t start = placed->start;
        std::int64_t length = placed->length;
        int in = placed->in;
        if (start < cursor) {
            // Overlapping clips are not allowed on a track except through transitions (rendered on their layer).
            const std::int64_t skip = cursor - start;
            length -= skip;
            start = cursor;
            in += static_cast<int>(skip);
            m_warnings << u"clip %1: overlaps the previous clip"_s.arg(modelClip.id.toString());
            if (length <= 0) {
                continue;
            }
        }
        if (start > cursor) {
            addBlank(start - cursor);
        }
        cursor = start + length;
        if (!placed->producer) {
            addBlank(length);
            continue;
        }
        const QByteArray key = placed->render ? placed->render->key : QByteArray();
        entries.push_back(Entry{placed->producer, in, in + static_cast<int>(length) - 1, placed->render, key});
    }
    return entries;
}

std::shared_ptr<Mlt::Producer> TimelineProjection::transitionProducer(TrackSlot &slot, const Transition &transition,
                                                                      const Placed &from, const Placed &to,
                                                                      std::int64_t windowStart, std::int64_t length)
{
    const fx::TransitionPreset *preset = transition.type.pack == QLatin1StringView(fx::Library::kCorePack)
                                             ? fx::Library::core().transition(transition.type.id)
                                             : nullptr;
    if (!preset) {
        m_warnings << u"transition %1 is not installed"_s.arg(transition.type.id);
        return nullptr;
    }
    TransitionSettings settings;
    settings.kind = preset->kernel;
    QString easing = preset->params.value(u"easing"_s).toString();
    if (const auto it = transition.params.find(u"easing"_s); it != transition.params.end()) {
        const ParamValue value = valueOf(it->second);
        if (std::holds_alternative<QString>(value)) {
            easing = std::get<QString>(value);
        }
    }
    settings.easing = easingOf(easing);
    settings.softness = preset->params.value(u"softness"_s).toDouble(0.02);
    if (const auto it = transition.params.find(u"softness"_s); it != transition.params.end()) {
        settings.softness = numberOf(it->second, settings.softness);
    }

    // The frames each side shows during [windowStart, windowStart + length): its own frames beyond the cut when the
    // material exists, else its edge frame frozen (FILE_FORMAT §5.8 fillMissing "freeze").
    struct Side
    {
        const Placed *placed;
        std::int64_t first;  // producer frame for windowStart (may be outside the material)
    };
    const Side sides[2] = {{&from, from.in + (windowStart - from.start)}, {&to, to.in + (windowStart - to.start)}};
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << int(settings.kind) << int(settings.easing) << settings.softness << qint64(length);
    for (const Side &side : sides) {
        stream << reinterpret_cast<quintptr>(side.placed->producer.get()) << qint64(side.first)
               << (side.placed->render ? side.placed->render->key : QByteArray());
    }
    if (auto existing = slot.transitions.value(key)) {
        return existing;
    }

    struct Parts
    {
        std::unique_ptr<Mlt::Tractor> tractor;
        std::unique_ptr<Mlt::Playlist> a;
        std::unique_ptr<Mlt::Playlist> b;
        std::unique_ptr<Mlt::Transition> transition;
    };
    auto parts = std::make_shared<Parts>();
    parts->tractor = std::make_unique<Mlt::Tractor>(m_profile);
    parts->a = std::make_unique<Mlt::Playlist>(m_profile);
    parts->b = std::make_unique<Mlt::Playlist>(m_profile);
    Mlt::Playlist *playlists[2] = {parts->a.get(), parts->b.get()};
    for (int i = 0; i < 2; ++i) {
        const Placed &placed = *sides[i].placed;
        Mlt::Playlist &playlist = *playlists[i];
        if (!placed.producer) {
            playlist.blank(static_cast<int>(length - 1));
            continue;
        }
        const std::int64_t available = placed.producer->get_length();
        std::int64_t first = sides[i].first;
        std::int64_t last = first + length - 1;
        const auto appendCut = [&](int in, int out, int repeat) {
            playlist.append(*placed.producer, in, out);
            if (repeat > 1) {
                playlist.repeat(playlist.count() - 1, repeat);
            }
            if (placed.render) {
                std::unique_ptr<Mlt::Producer> cut(playlist.get_clip(playlist.count() - 1));
                attachFilters(*cut, *placed.render, false);
            }
        };
        if (first < 0) {
            appendCut(0, 0, static_cast<int>(-first)); // before the material: the first frame frozen
            first = 0;
        }
        const std::int64_t end = std::min(last, available - 1);
        if (end >= first) {
            appendCut(static_cast<int>(first), static_cast<int>(end), 1);
        }
        if (last > end) {
            const std::int64_t missing = last - std::max(end, first - 1);
            appendCut(static_cast<int>(available - 1), static_cast<int>(available - 1), static_cast<int>(missing));
        }
    }
    parts->tractor->set_track(*parts->a, 0);
    parts->tractor->set_track(*parts->b, 1);
    parts->transition = makeTransition(m_profile, settings);
    parts->transition->set_in_and_out(0, static_cast<int>(length - 1));
    parts->tractor->plant_transition(*parts->transition, 0, 1);
    // Aliasing: the producer pointer keeps every part of the small graph alive.
    std::shared_ptr<Mlt::Producer> producer(parts, parts->tractor.get());
    slot.transitions.insert(key, producer);
    return producer;
}

std::vector<TimelineProjection::Entry> TimelineProjection::transitionEntries(TrackSlot &slot, const Track &track,
                                                                             const ProjectData &project)
{
    std::vector<Entry> entries;
    slot.media.clear();
    std::vector<Transition> transitions = track.transitions;
    if (m_preview.transition && m_preview.transitionTrack == track.id) {
        const auto same = std::find_if(transitions.begin(), transitions.end(), [&](const Transition &t) {
            return t.from == m_preview.transition->from && t.to == m_preview.transition->to;
        });
        if (same != transitions.end()) {
            *same = *m_preview.transition;
        } else {
            transitions.push_back(*m_preview.transition);
        }
    }
    const Sequence *sequence = sequenceOf(project, m_sequenceId);
    const bool mainTrack = sequence && !sequence->visualTracks.empty() && sequence->visualTracks.front().id == track.id;
    struct Window
    {
        std::int64_t start;
        std::int64_t length;
        std::shared_ptr<Mlt::Producer> producer;
    };
    std::vector<Window> windows;
    QHash<QByteArray, std::shared_ptr<Mlt::Producer>> used;
    for (const Transition &transition : transitions) {
        const Clip *from = track.findClip(transition.from);
        const Clip *to = track.findClip(transition.to);
        if (!from || !to || !(previewed(*from).end() == previewed(*to).start)) {
            continue;
        }
        const std::optional<Placed> a = place(*from, track, project, mainTrack, slot.media);
        const std::optional<Placed> b = place(*to, track, project, mainTrack, slot.media);
        if (!a || !b) {
            continue;
        }
        const std::int64_t cut = b->start;
        const std::int64_t length = std::clamp<std::int64_t>(toFrames(transition.duration), 1, std::min(a->length, b->length));
        const std::int64_t start = cut - length / 2;
        std::shared_ptr<Mlt::Producer> producer = transitionProducer(slot, transition, *a, *b, start, length);
        if (producer) {
            windows.push_back({start, length, producer});
        }
    }
    std::sort(windows.begin(), windows.end(), [](const Window &x, const Window &y) { return x.start < y.start; });
    std::int64_t cursor = 0;
    for (const Window &window : windows) {
        if (window.start < cursor) {
            m_warnings << u"two transitions overlap: the second is skipped"_s;
            continue;
        }
        if (window.start > cursor) {
            entries.push_back(Entry{nullptr, 0, static_cast<int>(window.start - cursor - 1), nullptr, {}});
        }
        QByteArray key = QByteArray::number(reinterpret_cast<quintptr>(window.producer.get()));
        entries.push_back(Entry{window.producer, 0, static_cast<int>(window.length - 1), nullptr, key});
        cursor = window.start + window.length;
        for (auto it = slot.transitions.constBegin(); it != slot.transitions.constEnd(); ++it) {
            if (it.value() == window.producer) {
                used.insert(it.key(), it.value());
            }
        }
    }
    slot.transitions = used; // tractors no longer used are dropped (the playlist and retired cuts keep them alive)
    return entries;
}

void TimelineProjection::attachFilters(Mlt::Producer &cut, const ClipRender &render, bool withAudio)
{
    if (render.chromaKey) {
        auto filter = makeChromaKeyFilter(m_profile, *render.chromaKey);
        cut.attach(*filter);
    }
    if (render.maskSettings) {
        auto filter = makeMaskFilter(m_profile, *render.maskSettings);
        cut.attach(*filter);
    }
    for (const AdjustSettings &adjust : render.adjusts) {
        auto filter = makeAdjustFilter(m_profile, adjust);
        cut.attach(*filter);
    }
    if (render.transform) {
        auto filter = makeTransformFilter(m_profile, *render.transform);
        cut.attach(*filter);
    }
    if (withAudio && render.gain) {
        auto filter = makeGainFilter(m_profile, *render.gain);
        cut.attach(*filter);
    }
}

void TimelineProjection::updateBackground()
{
    int duration = 1;
    for (const TrackSlot &slot : m_tracks) {
        duration = std::max(duration, slot.playlist->get_playtime());
    }
    m_duration = duration;
    if (duration == m_backgroundLength) {
        return;
    }
    m_backgroundLength = duration;
    retireEntries(*m_background);
    m_background->clear();
    m_background->append(*m_black, 0, duration - 1);
}

void TimelineProjection::patch(TrackSlot &slot, std::vector<Entry> desired)
{
    Mlt::Playlist &playlist = *slot.playlist;
    const std::vector<Entry> &current = slot.entries;
    size_t prefix = 0;
    while (prefix < current.size() && prefix < desired.size() && current[prefix] == desired[prefix]) {
        ++prefix;
    }
    size_t suffix = 0;
    while (suffix < current.size() - prefix && suffix < desired.size() - prefix &&
           current[current.size() - 1 - suffix] == desired[desired.size() - 1 - suffix]) {
        ++suffix;
    }
    Retired retired;
    retired.since = std::chrono::steady_clock::now();
    for (int i = static_cast<int>(current.size() - suffix) - 1; i >= static_cast<int>(prefix); --i) {
        if (Mlt::Producer *cut = playlist.get_clip(i)) {
            retired.cuts.emplace_back(cut); // a new reference: the cut outlives the playlist entry
        }
        playlist.remove(i);
    }
    if (!retired.cuts.empty()) {
        m_retired.push_back(std::move(retired));
    }
    int where = static_cast<int>(prefix);
    // At the end (building a track, media becoming ready) appending avoids moving every entry: O(n) instead of O(n²).
    const bool atEnd = suffix == 0;
    for (size_t k = prefix; k < desired.size() - suffix; ++k, ++where) {
        const Entry &entry = desired[k];
        if (!entry.producer) {
            if (atEnd) {
                playlist.blank(entry.out);
            } else {
                playlist.insert_blank(where, entry.out);
            }
            continue;
        }
        if (atEnd) {
            playlist.append(*entry.producer, entry.in, entry.out);
        } else {
            playlist.insert(*entry.producer, where, entry.in, entry.out);
        }
        if (entry.render) {
            std::unique_ptr<Mlt::Producer> cut(playlist.get_clip(where));
            attachFilters(*cut, *entry.render, slot.kind == SlotKind::Clips);
        }
    }
    slot.entries = std::move(desired);
}

void TimelineProjection::retireEntries(Mlt::Playlist &playlist)
{
    Retired retired;
    retired.since = std::chrono::steady_clock::now();
    for (int i = 0; i < playlist.count(); ++i) {
        // get_clip() returns a new reference to the entry's cut.
        if (Mlt::Producer *cut = playlist.get_clip(i)) {
            retired.cuts.emplace_back(cut);
        }
    }
    if (!retired.cuts.empty()) {
        m_retired.push_back(std::move(retired));
    }
}

void TimelineProjection::releaseRetired(std::chrono::milliseconds olderThan)
{
    const auto limit = std::chrono::steady_clock::now() - olderThan;
    std::erase_if(m_retired, [limit](const Retired &retired) { return retired.since <= limit; });
}

bool TimelineProjection::needsRebuild(const ProjectData &project, const ChangeSet &changes) const
{
    const Sequence *sequence = sequenceOf(project, m_sequenceId);
    if (!m_tractor || !sequence || changes.settingsChanged || changes.sequences.contains(m_sequenceId)) {
        return true;
    }
    const std::vector<StructureSlot> structure = structureOf(*sequence);
    if (structure.size() != m_tracks.size()) {
        return true;
    }
    for (size_t i = 0; i < structure.size(); ++i) {
        if (structure[i].id != m_tracks[i].id || structure[i].kind != m_tracks[i].kind) {
            return true;
        }
    }
    return false;
}

bool TimelineProjection::update(const ProjectData &project, const ChangeSet &changes)
{
    if (needsRebuild(project, changes)) {
        build(project, m_sequenceId);
        return true;
    }
    const Sequence *sequence = sequenceOf(project, m_sequenceId);
    bool anySolo = false;
    for (const Track &track : sequence->audioTracks) {
        anySolo = anySolo || track.solo;
    }
    m_warnings.clear();
    m_tractor->lock();
    for (TrackSlot &slot : m_tracks) {
        const Track *track = project.findTrack(slot.id);
        bool usesChangedMedia = false;
        for (const MediaId &media : changes.media) {
            usesChangedMedia = usesChangedMedia || slot.media.contains(media);
        }
        const bool previewed = (m_preview.clip && track->findClip(m_preview.clip->id)) ||
                               (m_preview.transition && m_preview.transitionTrack == slot.id);
        if (changes.tracks.contains(slot.id) || usesChangedMedia || previewed) {
            fillSlot(slot, *track, project, anySolo);
        } else if (slot.kind == SlotKind::Clips) {
            // Mute/solo of another track may change this one's audio.
            slot.playlist->set("hide", hideFlags(*track, slot.audio, anySolo));
        }
    }
    updateBackground();
    m_tractor->unlock();
    return false;
}

void TimelineProjection::mediaReady(const ProjectData &project, const MediaId &mediaId)
{
    ChangeSet changes;
    changes.media.insert(mediaId);
    update(project, changes);
}

void TimelineProjection::setPreview(const ProjectData &project, Preview preview)
{
    if (!m_tractor) {
        m_preview = std::move(preview);
        return;
    }
    // The tracks of the old and of the new preview are re-projected.
    ChangeSet changes;
    const auto mark = [&](const Preview &p) {
        if (p.clip) {
            for (const TrackSlot &slot : m_tracks) {
                if (const Track *track = project.findTrack(slot.id); track && track->findClip(p.clip->id)) {
                    changes.tracks.insert(slot.id);
                }
            }
        }
        if (p.transition && p.transitionTrack) {
            changes.tracks.insert(*p.transitionTrack);
        }
    };
    mark(m_preview);
    m_preview = std::move(preview);
    mark(m_preview);
    update(project, changes);
}

QImage TimelineProjection::renderFrame(int position)
{
    if (!m_tractor) {
        return {};
    }
    m_tractor->seek(position);
    std::unique_ptr<Mlt::Frame> frame(m_tractor->get_frame());
    if (!frame) {
        return {};
    }
    mlt_image_format format = mlt_image_rgba;
    int width = m_profile.width();
    int height = m_profile.height();
    const uint8_t *data = frame->get_image(format, width, height);
    if (!data || format != mlt_image_rgba) {
        return {};
    }
    return QImage(data, width, height, width * 4, QImage::Format_RGBA8888).copy();
}

} // namespace vedit::engine
