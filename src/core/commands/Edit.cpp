// SPDX-License-Identifier: GPL-3.0-or-later
#include "Edit.h"

namespace velacut {

namespace {

// Edit defined by two closures (for insert/remove pairs that never merge).
class FunctionEdit final : public Edit
{
public:
    FunctionEdit(std::function<void(ProjectMutator &)> apply, std::function<void(ProjectMutator &)> revert)
        : m_apply(std::move(apply))
        , m_revert(std::move(revert))
    {
    }
    void apply(ProjectMutator &mutator) override { m_apply(mutator); }
    void revert(ProjectMutator &mutator) override { m_revert(mutator); }

private:
    std::function<void(ProjectMutator &)> m_apply;
    std::function<void(ProjectMutator &)> m_revert;
};

// "Set value X of entity K from before to after". Consecutive replacements of the same (kind, key)
// merge into one: before of the first, after of the last.
template<typename Value>
class ReplaceEdit final : public Edit
{
public:
    using Setter = void (*)(ProjectMutator &, const QString &key, const Value &value);

    ReplaceEdit(QString key, Value before, Value after, Setter setter)
        : m_key(std::move(key))
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_setter(setter)
    {
    }
    void apply(ProjectMutator &mutator) override { m_setter(mutator, m_key, m_after); }
    void revert(ProjectMutator &mutator) override { m_setter(mutator, m_key, m_before); }
    bool canAbsorb(const Edit &next) const override
    {
        const auto *other = dynamic_cast<const ReplaceEdit<Value> *>(&next);
        return other && other->m_setter == m_setter && other->m_key == m_key;
    }
    void absorb(Edit &next) override { m_after = std::move(static_cast<ReplaceEdit<Value> &>(next).m_after); }

private:
    QString m_key;
    Value m_before;
    Value m_after;
    Setter m_setter;
};

template<typename Value>
std::unique_ptr<Edit> makeReplace(QString key, Value before, Value after, typename ReplaceEdit<Value>::Setter setter)
{
    return std::make_unique<ReplaceEdit<Value>>(std::move(key), std::move(before), std::move(after), setter);
}

std::unique_ptr<Edit> makeFunction(std::function<void(ProjectMutator &)> apply,
                                   std::function<void(ProjectMutator &)> revert)
{
    return std::make_unique<FunctionEdit>(std::move(apply), std::move(revert));
}

struct TrackTransition
{
    TrackId trackId;
    Transition transition;
};

struct SequenceCanvas
{
    SequenceId sequenceId;
    Canvas canvas;
};

struct SequenceBackground
{
    SequenceId sequenceId;
    std::optional<CanvasBackground> background;
};

} // namespace

namespace edits {

std::unique_ptr<Edit> setName(QString before, QString after)
{
    return makeReplace<QString>(QStringLiteral("name"), std::move(before), std::move(after),
                                [](ProjectMutator &m, const QString &, const QString &v) { m.setName(v); });
}

std::unique_ptr<Edit> setSettings(ProjectSettings before, ProjectSettings after)
{
    return makeReplace<ProjectSettings>(
        QStringLiteral("settings"), std::move(before), std::move(after),
        [](ProjectMutator &m, const QString &, const ProjectSettings &v) { m.setSettings(v); });
}

std::unique_ptr<Edit> insertMedia(int index, Media media)
{
    const MediaId id = media.id;
    return makeFunction([index, media = std::move(media)](ProjectMutator &m) { m.insertMedia(index, media); },
                        [id](ProjectMutator &m) { m.removeMedia(id); });
}

std::unique_ptr<Edit> removeMedia(int index, Media media)
{
    const MediaId id = media.id;
    return makeFunction([id](ProjectMutator &m) { m.removeMedia(id); },
                        [index, media = std::move(media)](ProjectMutator &m) { m.insertMedia(index, media); });
}

std::unique_ptr<Edit> replaceMedia(Media before, Media after)
{
    const QString key = before.id.toString();
    return makeReplace<Media>(key, std::move(before), std::move(after),
                              [](ProjectMutator &m, const QString &, const Media &v) { m.replaceMedia(v); });
}

std::unique_ptr<Edit> insertSequence(int index, Sequence sequence)
{
    const SequenceId id = sequence.id;
    return makeFunction([index, sequence = std::move(sequence)](ProjectMutator &m) { m.insertSequence(index, sequence); },
                        [id](ProjectMutator &m) { m.removeSequence(id); });
}

std::unique_ptr<Edit> removeSequence(int index, Sequence sequence)
{
    const SequenceId id = sequence.id;
    return makeFunction([id](ProjectMutator &m) { m.removeSequence(id); },
                        [index, sequence = std::move(sequence)](ProjectMutator &m) { m.insertSequence(index, sequence); });
}

std::unique_ptr<Edit> replaceSequence(Sequence before, Sequence after)
{
    const QString key = before.id.toString();
    return makeReplace<Sequence>(key, std::move(before), std::move(after),
                                 [](ProjectMutator &m, const QString &, const Sequence &v) { m.replaceSequence(v); });
}

std::unique_ptr<Edit> setCanvas(SequenceId sequenceId, Canvas before, Canvas after)
{
    return makeReplace<SequenceCanvas>(
        sequenceId.toString(), SequenceCanvas{sequenceId, before}, SequenceCanvas{sequenceId, after},
        [](ProjectMutator &m, const QString &, const SequenceCanvas &v) { m.setCanvas(v.sequenceId, v.canvas); });
}

std::unique_ptr<Edit> setDefaultBackground(SequenceId sequenceId, std::optional<CanvasBackground> before,
                                           std::optional<CanvasBackground> after)
{
    return makeReplace<SequenceBackground>(sequenceId.toString(), SequenceBackground{sequenceId, std::move(before)},
                                           SequenceBackground{sequenceId, std::move(after)},
                                           [](ProjectMutator &m, const QString &, const SequenceBackground &v) {
                                               m.setDefaultBackground(v.sequenceId, v.background);
                                           });
}

std::unique_ptr<Edit> setSequenceMarkers(SequenceId sequenceId, std::vector<Marker> before, std::vector<Marker> after)
{
    return makeFunction([sequenceId, after = std::move(after)](ProjectMutator &m) { m.setSequenceMarkers(sequenceId, after); },
                        [sequenceId, before = std::move(before)](ProjectMutator &m) {
                            m.setSequenceMarkers(sequenceId, before);
                        });
}

std::unique_ptr<Edit> setGroups(SequenceId sequenceId, std::vector<Group> before, std::vector<Group> after)
{
    return makeFunction([sequenceId, after = std::move(after)](ProjectMutator &m) { m.setGroups(sequenceId, after); },
                        [sequenceId, before = std::move(before)](ProjectMutator &m) { m.setGroups(sequenceId, before); });
}

std::unique_ptr<Edit> insertTrack(SequenceId sequenceId, bool audioTrack, int index, Track track)
{
    const TrackId id = track.id;
    return makeFunction([sequenceId, audioTrack, index, track = std::move(track)](ProjectMutator &m) {
        m.insertTrack(sequenceId, audioTrack, index, track);
    },
                        [id](ProjectMutator &m) { m.removeTrack(id); });
}

std::unique_ptr<Edit> removeTrack(SequenceId sequenceId, bool audioTrack, int index, Track track)
{
    const TrackId id = track.id;
    return makeFunction([id](ProjectMutator &m) { m.removeTrack(id); },
                        [sequenceId, audioTrack, index, track = std::move(track)](ProjectMutator &m) {
                            m.insertTrack(sequenceId, audioTrack, index, track);
                        });
}

std::unique_ptr<Edit> setTrackProperties(Track before, Track after)
{
    // Only properties are applied; clips and transitions are ignored by the setter.
    before.clips.clear();
    before.transitions.clear();
    after.clips.clear();
    after.transitions.clear();
    const QString key = before.id.toString();
    return makeReplace<Track>(key, std::move(before), std::move(after),
                              [](ProjectMutator &m, const QString &, const Track &v) { m.setTrackProperties(v); });
}

std::unique_ptr<Edit> insertClip(TrackId trackId, Clip clip)
{
    const ClipId id = clip.id;
    return makeFunction([trackId, clip = std::move(clip)](ProjectMutator &m) { m.insertClip(trackId, clip); },
                        [id](ProjectMutator &m) { m.removeClip(id); });
}

std::unique_ptr<Edit> removeClip(TrackId trackId, Clip clip)
{
    const ClipId id = clip.id;
    return makeFunction([id](ProjectMutator &m) { m.removeClip(id); },
                        [trackId, clip = std::move(clip)](ProjectMutator &m) { m.insertClip(trackId, clip); });
}

std::unique_ptr<Edit> replaceClip(Clip before, Clip after)
{
    const QString key = before.id.toString();
    return makeReplace<Clip>(key, std::move(before), std::move(after),
                             [](ProjectMutator &m, const QString &, const Clip &v) { m.replaceClip(v); });
}

std::unique_ptr<Edit> shiftClips(TrackId trackId, std::vector<ClipId> clipIds, RationalTime delta)
{
    return makeFunction([trackId, clipIds, delta](ProjectMutator &m) { m.shiftClips(trackId, clipIds, delta); },
                        [trackId, clipIds, delta](ProjectMutator &m) { m.shiftClips(trackId, clipIds, -delta); });
}

std::unique_ptr<Edit> insertTransition(TrackId trackId, Transition transition)
{
    const TransitionId id = transition.id;
    return makeFunction(
        [trackId, transition = std::move(transition)](ProjectMutator &m) { m.insertTransition(trackId, transition); },
        [trackId, id](ProjectMutator &m) { m.removeTransition(trackId, id); });
}

std::unique_ptr<Edit> removeTransition(TrackId trackId, Transition transition)
{
    const TransitionId id = transition.id;
    return makeFunction(
        [trackId, id](ProjectMutator &m) { m.removeTransition(trackId, id); },
        [trackId, transition = std::move(transition)](ProjectMutator &m) { m.insertTransition(trackId, transition); });
}

std::unique_ptr<Edit> replaceTransition(TrackId trackId, Transition before, Transition after)
{
    const QString key = before.id.toString();
    return makeReplace<TrackTransition>(
        key, TrackTransition{trackId, std::move(before)}, TrackTransition{trackId, std::move(after)},
        [](ProjectMutator &m, const QString &, const TrackTransition &v) { m.replaceTransition(v.trackId, v.transition); });
}

} // namespace edits

} // namespace velacut
