// SPDX-License-Identifier: GPL-3.0-or-later
#include "TranscriptController.h"

#include "ai/Transcript.h"
#include "core/edit/TimelineEditor.h"
#include "engine/playback/TimelinePlayer.h"
#include "ui/controllers/AiController.h"
#include "ui/controllers/EditorController.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;

namespace velacut::ui {

namespace {

// A new paragraph after a sentence and a pause this long.
constexpr std::int64_t kParagraphPauseMs = 700;

bool endsSentence(const QString &word)
{
    static const QRegularExpression end(u"[.!?\u2026][\"'\u201D\u00BB)]*$"_s);
    return end.match(word).hasMatch();
}

} // namespace

TranscriptController::TranscriptController(EditorController &editor)
    : QObject(&editor)
    , m_editor(editor)
{
    const auto refresh = [this] {
        m_dirty = true;
        emit changed();
        updateCurrent();
    };
    connect(&editor, &EditorController::modelChanged, this, refresh);
    connect(editor.ai(), &AiController::transcriptsChanged, this, refresh);
    connect(editor.player(), &engine::TimelinePlayer::positionChanged, this, &TranscriptController::updateCurrent);
}

const std::vector<TranscriptController::Entry> &TranscriptController::entries() const
{
    if (!m_dirty) {
        return m_entries;
    }
    m_dirty = false;
    m_entries.clear();
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return m_entries;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    for (const Clip &clip : sequence->visualTracks.front().clips) {
        const MediaClipData *media = clip.media();
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (!item || media->reversed || media->curve) {
            continue;
        }
        const ai::Transcript *transcript = m_editor.ai()->transcriptOf(*item);
        if (!transcript) {
            continue;
        }
        const std::int64_t from = media->sourceIn.rescaled(Rational(1000), Rounding::NearestEven).value();
        const std::int64_t to = from + std::llround(clip.duration.toSecondsDouble() * media->speed * 1000.0);
        const std::int64_t start = clip.start.rescaled(rate, Rounding::NearestEven).value();
        const auto frameOf = [&](std::int64_t ms) {
            return start + std::llround(static_cast<double>(ms - from) / media->speed * rate.toDouble() / 1000.0);
        };
        for (const ai::Transcript::Word &word : transcript->words) {
            // Words said (mostly) inside the part the clip plays.
            const std::int64_t middle = (word.from + word.to) / 2;
            if (middle < from || middle >= to) {
                continue;
            }
            Entry entry{clip.id, word.text, word.from, word.to, frameOf(std::max(word.from, from)), frameOf(std::min(word.to, to)),
                        ai::isFillerWord(word.text), false};
            if (!m_entries.empty()) {
                const Entry &previous = m_entries.back();
                entry.breakBefore = previous.clip != entry.clip ||
                                    (endsSentence(previous.text) && entry.fromMs - previous.toMs >= kParagraphPauseMs);
            }
            m_entries.push_back(std::move(entry));
        }
    }
    return m_entries;
}

bool TranscriptController::available() const
{
    return !entries().empty();
}

bool TranscriptController::incomplete() const
{
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return false;
    }
    for (const Clip &clip : sequence->visualTracks.front().clips) {
        const MediaClipData *media = clip.media();
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (item && item->info.audio && media->streams != Streams::VideoOnly && !m_editor.ai()->transcriptOf(*item)) {
            return true;
        }
    }
    return false;
}

QVariantList TranscriptController::paragraphs() const
{
    QVariantList result;
    QVariantList words;
    int first = 0;
    const std::vector<Entry> &list = entries();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].breakBefore && !words.isEmpty()) {
            result << QVariantMap{{u"first"_s, first}, {u"words"_s, words}};
            words.clear();
            first = static_cast<int>(i);
        }
        words << QVariantMap{{u"text"_s, list[i].text}, {u"filler"_s, list[i].filler}};
    }
    if (!words.isEmpty()) {
        result << QVariantMap{{u"first"_s, first}, {u"words"_s, words}};
    }
    return result;
}

int TranscriptController::wordCount() const
{
    return static_cast<int>(entries().size());
}

int TranscriptController::fillerCount() const
{
    const std::vector<Entry> &list = entries();
    return static_cast<int>(std::count_if(list.begin(), list.end(), [](const Entry &e) { return e.filler; }));
}

void TranscriptController::updateCurrent()
{
    int current = -1;
    const std::int64_t playhead = m_editor.player()->position();
    const std::vector<Entry> &list = entries();
    for (size_t i = 0; i < list.size() && list[i].frame <= playhead; ++i) {
        if (playhead < list[i].endFrame || (i + 1 < list.size() && playhead < list[i + 1].frame)) {
            current = static_cast<int>(i);
        }
    }
    if (current != m_current) {
        m_current = current;
        emit currentChanged();
    }
}

void TranscriptController::seekToWord(int index)
{
    const std::vector<Entry> &list = entries();
    if (index >= 0 && index < static_cast<int>(list.size())) {
        m_editor.player()->seek(static_cast<int>(list[static_cast<size_t>(index)].frame));
    }
}

bool TranscriptController::cut(const std::vector<const Entry *> &words, const QString &text)
{
    std::map<ClipId, std::vector<std::pair<RationalTime, RationalTime>>> ranges;
    for (const Entry *word : words) {
        ranges[word->clip].emplace_back(RationalTime(word->fromMs, Rational(1000)), RationalTime(word->toMs, Rational(1000)));
    }
    if (ranges.empty()) {
        return false;
    }
    return m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).removeSourceRanges(ranges, text));
}

bool TranscriptController::deleteWords(int first, int last)
{
    const std::vector<Entry> &list = entries();
    first = std::max(0, std::min(first, last));
    last = std::min(static_cast<int>(list.size()) - 1, std::max(first, last));
    std::vector<const Entry *> words;
    for (int i = first; i <= last; ++i) {
        words.push_back(&list[static_cast<size_t>(i)]);
    }
    if (!cut(words, tr("Delete words"))) {
        return false;
    }
    emit m_editor.message(tr("%n word(s) cut from the video", nullptr, static_cast<int>(words.size())), true);
    return true;
}

std::vector<ai::SpokenWord> TranscriptController::spokenWords() const
{
    const Rational rate = m_editor.data().settings.frameRate;
    const auto ms = [&rate](std::int64_t frame) { return std::llround(static_cast<double>(frame) * 1000.0 / rate.toDouble()); };
    std::vector<ai::SpokenWord> words;
    for (const Entry &entry : entries()) {
        words.push_back(ai::SpokenWord{entry.text, ms(entry.frame), ms(entry.endFrame)});
    }
    return words;
}

QString TranscriptController::makeChapters()
{
    const Rational rate = m_editor.data().settings.frameRate;
    const auto ms = [&rate](std::int64_t frame) { return std::llround(static_cast<double>(frame) * 1000.0 / rate.toDouble()); };
    const std::vector<ai::SpokenWord> words = spokenWords();
    const std::int64_t duration = ms(m_editor.player()->duration());
    const std::vector<ai::Chapter> chapters = ai::findChapters(words, duration);
    if (chapters.empty()) {
        emit m_editor.message(tr("Chapters need a video of at least 30 seconds with speech (YouTube wants 3 of 10 s or more)."), false);
        return {};
    }
    std::vector<std::pair<RationalTime, QString>> markers;
    for (const ai::Chapter &chapter : chapters) {
        markers.emplace_back(RationalTime(chapter.start, Rational(1000)), chapter.title);
    }
    if (!m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).setChapterMarkers(markers))) {
        return {};
    }
    const QString list = ai::chapterList(chapters, duration);
    QGuiApplication::clipboard()->setText(list);
    emit m_editor.message(tr("%n chapter(s) marked on the timeline; the list is copied for the description", nullptr,
                             static_cast<int>(chapters.size())),
                          true);
    return list;
}

int TranscriptController::removeFillerWords()
{
    std::vector<const Entry *> words;
    for (const Entry &entry : entries()) {
        if (entry.filler) {
            words.push_back(&entry);
        }
    }
    if (words.empty()) {
        emit m_editor.message(tr("No filler words found."), false);
        return 0;
    }
    const int count = static_cast<int>(words.size());
    if (!cut(words, tr("Remove filler words"))) {
        return 0;
    }
    emit m_editor.message(tr("%n filler word(s) removed", nullptr, count), true);
    return count;
}

} // namespace velacut::ui
