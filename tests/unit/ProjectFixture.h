// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/EditCommand.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/Project.h"

#include <QTest>
#include <QUndoStack>

#include <memory>

namespace velacut::test {

inline const Rational kRate(30);

inline RationalTime frames(std::int64_t n)
{
    return RationalTime(n, kRate);
}

inline Media makeMedia(MediaKind kind, const QString &name, std::optional<RationalTime> duration, bool audio)
{
    Media media;
    media.id = MediaId::create();
    media.kind = kind;
    media.name = name;
    media.path = QStringLiteral("/media/") + name;
    media.fingerprint = {QStringLiteral("sha256-sampled-v1"), QStringLiteral("00ff"), 1234};
    media.info.duration = duration;
    if (kind != MediaKind::Audio) {
        VideoStreamInfo video;
        video.width = 1920;
        video.height = 1080;
        video.frameRate = kRate;
        video.codec = QStringLiteral("h264");
        media.info.video = video;
    }
    if (audio) {
        media.info.audio = AudioStreamInfo{QStringLiteral("aac"), 48000, 2};
    }
    return media;
}

// A project at 30 fps with a few media items ready to be placed on the timeline.
struct Fixture
{
    ProjectData data = ProjectData::createEmpty(QStringLiteral("Test"));
    MediaId video10s;      // 300 frames, with audio
    MediaId video4sMute;   // 120 frames, no audio
    MediaId music20s;      // audio only, 20 s at 48 kHz
    MediaId photo;         // still image

    Fixture()
    {
        data.settings.frameRate = kRate;
        Media video = makeMedia(MediaKind::Video, QStringLiteral("video.mp4"), RationalTime(300, kRate), true);
        Media mute = makeMedia(MediaKind::Video, QStringLiteral("mute.mp4"), RationalTime(120, kRate), false);
        Media music = makeMedia(MediaKind::Audio, QStringLiteral("music.flac"), RationalTime(20 * 48000, Rational(48000)), true);
        Media image = makeMedia(MediaKind::Image, QStringLiteral("photo.jpg"), std::nullopt, false);
        video10s = video.id;
        video4sMute = mute.id;
        music20s = music.id;
        photo = image.id;
        data.media = {video, mute, music, image};
    }
};

// Describes the first difference between two projects (for readable test failures).
inline QString firstDifference(const ProjectData &a, const ProjectData &b)
{
    if (a.id != b.id || a.name != b.name) {
        return QStringLiteral("id/name");
    }
    if (a.createdAt != b.createdAt || a.modifiedAt != b.modifiedAt) {
        return QStringLiteral("dates: ") + a.createdAt.toString(Qt::ISODateWithMs) + QStringLiteral(" vs ") +
               b.createdAt.toString(Qt::ISODateWithMs);
    }
    if (!(a.settings == b.settings)) {
        return QStringLiteral("settings");
    }
    if (a.mediaFolders != b.mediaFolders) {
        return QStringLiteral("mediaFolders");
    }
    for (size_t i = 0; i < std::min(a.media.size(), b.media.size()); ++i) {
        if (!(a.media[i] == b.media[i])) {
            const Media &x = a.media[i];
            const Media &y = b.media[i];
            return QStringLiteral("media[%1] %2").arg(i).arg(!(x.info == y.info) ? QStringLiteral("info") : !(x.fingerprint == y.fingerprint) ? QStringLiteral("fingerprint") : QStringLiteral("other"));
        }
    }
    if (a.media.size() != b.media.size()) {
        return QStringLiteral("media count");
    }
    for (size_t s = 0; s < std::min(a.sequences.size(), b.sequences.size()); ++s) {
        const Sequence &x = a.sequences[s];
        const Sequence &y = b.sequences[s];
        for (int pass = 0; pass < 2; ++pass) {
            const auto &tx = pass == 0 ? x.visualTracks : x.audioTracks;
            const auto &ty = pass == 0 ? y.visualTracks : y.audioTracks;
            if (tx.size() != ty.size()) {
                return QStringLiteral("sequence %1 track count").arg(s);
            }
            for (size_t t = 0; t < tx.size(); ++t) {
                if (tx[t] == ty[t]) {
                    continue;
                }
                if (tx[t].clips.size() != ty[t].clips.size()) {
                    return QStringLiteral("track %1 clip count").arg(t);
                }
                for (size_t c = 0; c < tx[t].clips.size(); ++c) {
                    const Clip &p = tx[t].clips[c];
                    const Clip &q = ty[t].clips[c];
                    if (p == q) {
                        continue;
                    }
                    QStringList parts;
                    if (!(p.transform == q.transform)) parts << QStringLiteral("transform");
                    if (!(p.opacity == q.opacity)) parts << QStringLiteral("opacity");
                    if (p.effects != q.effects) parts << QStringLiteral("effects");
                    if (p.markers != q.markers) parts << QStringLiteral("markers");
                    if (!(p.payload == q.payload)) parts << QStringLiteral("payload");
                    if (p.extras != q.extras) parts << QStringLiteral("extras");
                    if (!(p.start == q.start) || !(p.duration == q.duration)) parts << QStringLiteral("times");
                    return QStringLiteral("track %1 clip %2: %3").arg(t).arg(c).arg(parts.join(QLatin1Char(',')));
                }
                return QStringLiteral("track %1 properties/transitions").arg(t);
            }
        }
        if (!(x == y)) {
            return QStringLiteral("sequence %1 other").arg(s);
        }
    }
    return a == b ? QString() : QStringLiteral("other");
}

// A live project with an undo stack. apply() pushes a command and verifies that redo -> undo -> redo
// restores exactly the same data and that every state satisfies the invariants.
class Session
{
public:
    explicit Session(ProjectData data)
        : project(std::move(data))
    {
    }

    const ProjectData &data() const { return project.data(); }
    const Sequence &sequence() const { return *project.data().mainSequence(); }
    const Track &mainTrack() const { return sequence().visualTracks.front(); }
    TimelineEditor editor() const { return TimelineEditor(project.data(), project.data().mainSequenceId); }

    // Returns false (with a test failure message) if anything is inconsistent.
    [[nodiscard]] bool apply(EditResult result)
    {
        if (!result.ok()) {
            qWarning("edit failed: %s", qPrintable(result.error));
            return false;
        }
        const ProjectData before = project.data();
        stack.push(new EditCommand(project, result.text, std::move(result.script)));
        const ProjectData after = project.data();
        if (!checkInvariants(after)) {
            return false;
        }
        stack.undo();
        if (!(project.data() == before)) {
            qWarning("undo did not restore the previous state");
            return false;
        }
        stack.redo();
        if (!(project.data() == after)) {
            qWarning("redo did not restore the edited state");
            return false;
        }
        return true;
    }

    static bool checkInvariants(const ProjectData &data)
    {
        const QStringList errors = data.checkInvariants();
        for (const QString &error : errors) {
            qWarning("invariant violated: %s", qPrintable(error));
        }
        return errors.isEmpty();
    }

    Project project;
    QUndoStack stack;
};

} // namespace velacut::test
