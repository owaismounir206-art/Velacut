// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineProjection.h"

#include "engine/timeline/MediaProducerCache.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#include <mlt++/Mlt.h>

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
    m_retired.clear();
}

std::int64_t TimelineProjection::toFrames(const RationalTime &time) const
{
    // Identity when the profile has the project frame rate (D-04); otherwise the nearest frame.
    return time.rate() == m_rate ? time.value() : time.rescaled(m_rate, Rounding::NearestEven).value();
}

std::shared_ptr<Mlt::Producer> TimelineProjection::producerFor(const Media &media)
{
    return m_loading == MediaLoading::Wait ? m_cache.open(media) : m_cache.producerOrRequest(media);
}

void TimelineProjection::build(const ProjectData &project, const SequenceId &sequenceId)
{
    m_sequenceId = sequenceId;
    m_transitions.clear();
    m_tracks.clear();
    m_keepAlive.clear();
    m_warnings.clear();
    m_tractor = std::make_unique<Mlt::Tractor>(m_profile);
    m_black = std::make_unique<Mlt::Producer>(m_profile, mltColor(Color{0, 0, 0, 255}).constData());
    m_black->set("length", 0x7fffffff);
    m_background = std::make_unique<Mlt::Playlist>(m_profile);
    m_tractor->set_track(*m_background, 0);

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
    const auto addTrack = [&](const Track &track, bool audio) {
        TrackSlot slot;
        slot.id = track.id;
        slot.audio = audio;
        slot.playlist = std::make_unique<Mlt::Playlist>(m_profile);
        fillTrack(slot, track, project, anySolo);
        m_tractor->set_track(*slot.playlist, index);
        if (!audio) {
            auto composite = std::make_unique<Mlt::Transition>(m_profile, "vedit.composite");
            composite->set("always_active", 1);
            m_tractor->plant_transition(*composite, 0, index);
            m_transitions.push_back(std::move(composite));
        }
        auto mix = std::make_unique<Mlt::Transition>(m_profile, "mix");
        mix->set("always_active", 1);
        mix->set("sum", 1);
        m_tractor->plant_transition(*mix, 0, index);
        m_transitions.push_back(std::move(mix));
        m_tracks.push_back(std::move(slot));
        ++index;
    };
    for (const Track &track : sequence->visualTracks) {
        addTrack(track, false);
    }
    for (const Track &track : sequence->audioTracks) {
        addTrack(track, true);
    }
    updateBackground();
}

void TimelineProjection::fillTrack(TrackSlot &slot, const Track &track, const ProjectData &project, bool anySolo)
{
    Mlt::Playlist &playlist = *slot.playlist;
    retireEntries(playlist);
    playlist.clear();
    slot.media.clear();
    playlist.set("hide", hideFlags(track, slot.audio, anySolo));
    std::int64_t cursor = 0;
    for (const Clip &clip : track.clips) {
        // Both edges are converted (not start and duration) so that adjacent clips stay adjacent.
        std::int64_t start = toFrames(clip.start);
        std::int64_t length = toFrames(clip.end()) - start;
        if (length <= 0) {
            continue; // shorter than one frame at the output rate
        }
        if (start < cursor) {
            // Overlap transitions are rendered from Phase 2: until then the overlapping head is skipped.
            length -= cursor - start;
            start = cursor;
            m_warnings << u"clip %1: overlap transition not rendered yet"_s.arg(clip.id.toString());
            if (length <= 0) {
                continue;
            }
        }
        if (start > cursor) {
            playlist.blank(static_cast<int>(start - cursor - 1));
        }
        cursor = start + length;
        const int out = static_cast<int>(length - 1);
        if (!clip.enabled) {
            playlist.blank(out);
            continue;
        }
        if (const MediaClipData *data = clip.media()) {
            slot.media.insert(data->mediaId);
            const Media *media = project.findMedia(data->mediaId);
            std::shared_ptr<Mlt::Producer> producer = media ? producerFor(*media) : nullptr;
            if (!producer) {
                if (media && !m_cache.error(media->id).isEmpty()) {
                    m_warnings << m_cache.error(media->id);
                }
                playlist.blank(out);
                continue;
            }
            if (data->speed != 1.0 || data->curve || data->reversed) {
                m_warnings << u"clip %1: speed and reverse are rendered from Phase 2"_s.arg(clip.id.toString());
            }
            const int in = media->kind == MediaKind::Image ? 0 : static_cast<int>(toFrames(data->sourceIn));
            playlist.append(*producer, in, in + out);
            if (data->streams == Streams::VideoOnly && media->info.audio) {
                std::unique_ptr<Mlt::Producer> cut(playlist.get_clip(playlist.count() - 1));
                Mlt::Filter silence(m_profile, "volume");
                silence.set("gain", 0.0);
                cut->attach(silence);
            }
        } else if (const auto *color = std::get_if<ColorClipData>(&clip.payload)) {
            const ParamValue value = color->color.staticValue();
            const Color c = std::holds_alternative<Color>(value) ? std::get<Color>(value) : Color{};
            auto producer = std::make_shared<Mlt::Producer>(m_profile, mltColor(c).constData());
            producer->set("length", 0x7fffffff);
            playlist.append(*producer, 0, out);
            m_keepAlive.push_back(producer);
        } else {
            m_warnings << u"clip %1: this kind of clip is rendered from a later phase"_s.arg(clip.id.toString());
            playlist.blank(out);
        }
    }
}

void TimelineProjection::updateBackground()
{
    int duration = 1;
    for (const TrackSlot &slot : m_tracks) {
        duration = std::max(duration, slot.playlist->get_playtime());
    }
    m_duration = duration;
    retireEntries(*m_background);
    m_background->clear();
    m_background->append(*m_black, 0, duration - 1);
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
    std::vector<TrackId> ids;
    for (const Track &track : sequence->visualTracks) {
        ids.push_back(track.id);
    }
    for (const Track &track : sequence->audioTracks) {
        ids.push_back(track.id);
    }
    if (ids.size() != m_tracks.size()) {
        return true;
    }
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] != m_tracks[i].id) {
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
        if (changes.tracks.contains(slot.id) || usesChangedMedia) {
            fillTrack(slot, *track, project, anySolo);
        } else {
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
