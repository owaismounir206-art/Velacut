// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/Transcript.h"
#include "core/project/Id.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

namespace vedit::ui {

class EditorController;

// Editing by the transcript (SPEC §5.8, Phase 6 criterion): the words said in the main track's clips, in timeline
// order, from the transcripts of Editor.ai; deleting words cuts them out of the video (the rest stays together), and
// "Remove filler words" takes out the hesitations. Every change is one undo step of ordinary cuts.
class TranscriptController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TranscriptEditor)
    QML_UNCREATABLE("Provided by Editor.transcript")

    // Some clip of the main track has a transcript.
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    // Clips of the main track with sound and no transcript yet.
    Q_PROPERTY(bool incomplete READ incomplete NOTIFY changed FINAL)
    // [{first (index of its first word), words: [{text, filler}]}]: sentences, the unit of the list.
    Q_PROPERTY(QVariantList paragraphs READ paragraphs NOTIFY changed FINAL)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY changed FINAL)
    Q_PROPERTY(int fillerCount READ fillerCount NOTIFY changed FINAL)
    // The word being said at the playhead, -1 if none.
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged FINAL)

public:
    explicit TranscriptController(EditorController &editor);

    bool available() const;
    bool incomplete() const;
    QVariantList paragraphs() const;
    int wordCount() const;
    int fillerCount() const;
    int currentIndex() const { return m_current; }

    Q_INVOKABLE void seekToWord(int index);
    // Cuts the words first…last (indices of the whole list) out of the video.
    Q_INVOKABLE bool deleteWords(int first, int last);
    // The number of filler words taken out.
    Q_INVOKABLE int removeFillerWords();
    // YouTube chapters from the transcript: chapter markers on the timeline (one undo step) and the "00:00 Title" list
    // copied for the video's description. Returns the list, empty when the video is too short.
    Q_INVOKABLE QString makeChapters();
    // What is said on the main track, in timeline milliseconds.
    std::vector<ai::SpokenWord> spokenWords() const;

signals:
    void changed();
    void currentChanged();

private:
    struct Entry
    {
        ClipId clip;
        QString text;
        std::int64_t fromMs = 0; // in the clip's media
        std::int64_t toMs = 0;
        std::int64_t frame = 0;  // where it is said on the timeline
        std::int64_t endFrame = 0;
        bool filler = false;
        bool breakBefore = false;
    };
    const std::vector<Entry> &entries() const;
    bool cut(const std::vector<const Entry *> &words, const QString &text);
    void updateCurrent();

    EditorController &m_editor;
    mutable std::vector<Entry> m_entries;
    mutable bool m_dirty = true;
    int m_current = -1;
};

} // namespace vedit::ui
