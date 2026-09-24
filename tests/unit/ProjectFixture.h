// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/EditCommand.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/Project.h"

#include <QTest>
#include <QUndoStack>

#include <memory>

namespace vedit::test {

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

} // namespace vedit::test
