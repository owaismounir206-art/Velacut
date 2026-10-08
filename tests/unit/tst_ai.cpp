// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/AiTask.h"
#include "ai/Analysis.h"
#include "ai/Highlights.h"
#include "ai/Montage.h"
#include "ai/Script.h"
#include "ai/Transcript.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

namespace {

// Counts to `steps`, reporting its progress; stops when cancelled.
class CountingTask : public velacut::ai::AiTask
{
public:
    explicit CountingTask(int steps)
        : m_steps(steps)
    {
    }
    QString title() const override { return QStringLiteral("Counting"); }
    int counted = 0;

protected:
    QString run() override
    {
        for (int i = 0; i < m_steps && !isCanceled(); ++i) {
            QThread::msleep(2);
            ++counted;
            report(double(i + 1) / m_steps);
        }
        return counted == 3 ? QStringLiteral("three is not allowed") : QString();
    }

private:
    int m_steps;
};

} // namespace

using namespace velacut;
using namespace velacut::ai;
using namespace Qt::StringLiterals;

class TestAi : public QObject
{
    Q_OBJECT

private slots:
    void pausesFollowTheRecording()
    {
        // 100 windows per second: 1 s of speech (−20 dB), 1 s of room noise (−55 dB), 1 s of speech, 0.3 s of noise,
        // 0.5 s of speech, then 1 s of noise at the end.
        std::vector<float> levels;
        const auto add = [&levels](int windows, float level) { levels.insert(levels.end(), static_cast<size_t>(windows), level); };
        add(100, -20.0f);
        add(100, -55.0f);
        add(100, -20.0f);
        add(30, -55.0f);
        add(50, -20.0f);
        add(100, -55.0f);
        const std::vector<SourceRange> pauses = findPauses(levels, 100);
        QCOMPARE(pauses.size(), 2u);
        // The pause in the middle keeps 0.15 s on each side; the short one (0.3 s) stays.
        QCOMPARE(pauses[0].start, RationalTime(115, Rational(100)));
        QCOMPARE(pauses[0].end, RationalTime(185, Rational(100)));
        // At the end: all of it goes after the breathing room.
        QCOMPARE(pauses[1].start, RationalTime(395, Rational(100)));
        QCOMPARE(pauses[1].end, RationalTime(480, Rational(100)));
        // A louder room: same pauses (the threshold follows the noise floor).
        for (float &level : levels) {
            level = level < -50.0f ? -38.0f : -12.0f;
        }
        QCOMPARE(findPauses(levels, 100).size(), 2u);
        // All at one level: nothing to tell apart.
        QVERIFY(findPauses(std::vector<float>(500, -30.0f), 100).empty());
    }

    void tasksReportFinishFailAndCancel()
    {
        CountingTask quick(50);
        QSignalSpy finished(&quick, &ai::AiTask::finished);
        quick.start();
        QVERIFY(quick.running());
        QVERIFY(finished.wait(5000));
        QVERIFY(!quick.running());
        QCOMPARE(quick.counted, 50);
        QCOMPARE(quick.progress(), 1.0);

        CountingTask failing(3);
        QSignalSpy failed(&failing, &ai::AiTask::failed);
        failing.start();
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("three is not allowed"));

        CountingTask endless(1000000);
        QSignalSpy canceled(&endless, &ai::AiTask::canceled);
        QSignalSpy endlessFinished(&endless, &ai::AiTask::finished);
        endless.start();
        QTRY_VERIFY(endless.progress() > 0.0 || endless.counted > 0);
        endless.cancel();
        QVERIFY(canceled.wait(5000));
        QCOMPARE(endlessFinished.count(), 0);

        // Destroyed while running: it stops and waits.
        auto running = std::make_unique<CountingTask>(1000000);
        running->start();
        QThread::msleep(20);
        running.reset();
    }

    void scenesAreCutsThatStandOut()
    {
        std::vector<float> differences;
        std::vector<double> times;
        for (int i = 0; i < 150; ++i) {
            times.push_back(i / 30.0);
            // A slow pan (0.03 per frame), a cut at frame 45, a whip of 3 frames at 90 (not a cut: it is not alone),
            // a cut at 120.
            float value = 0.03f;
            if (i == 45 || i == 120) {
                value = 0.6f;
            } else if (i >= 90 && i < 93) {
                value = 0.4f;
            }
            differences.push_back(i == 0 ? 0.0f : value);
        }
        const std::vector<double> cuts = findSceneCuts(differences, times);
        QCOMPARE(cuts.size(), 2u);
        QCOMPARE(cuts[0], 45 / 30.0);
        QCOMPARE(cuts[1], 120 / 30.0);
        // Two cuts closer than the minimum: only the first.
        differences[50] = 0.7f;
        QCOMPARE(findSceneCuts(differences, times).size(), 2u);
    }

    void whisperOutputBecomesWords()
    {
        // The shape of `whisper-cli --output-json-full`: segments, tokens with offsets in ms, special tokens.
        const auto token = [](const char *text, int from, int to) {
            return QJsonObject{{QStringLiteral("text"), QString::fromUtf8(text)},
                               {QStringLiteral("offsets"), QJsonObject{{QStringLiteral("from"), from}, {QStringLiteral("to"), to}}}};
        };
        const QJsonObject json{
            {QStringLiteral("result"), QJsonObject{{QStringLiteral("language"), QStringLiteral("it")}}},
            {QStringLiteral("transcription"),
             QJsonArray{QJsonObject{{QStringLiteral("text"), QStringLiteral(" Ciao a tutti, benvenuti.")},
                                    {QStringLiteral("offsets"), QJsonObject{{QStringLiteral("from"), 0}, {QStringLiteral("to"), 2000}}},
                                    {QStringLiteral("tokens"),
                                     QJsonArray{token("[_BEG_]", 0, 0), token(" Ciao", 0, 400), token(" a", 400, 500),
                                                token(" tut", 500, 700), token("ti", 700, 900), token(",", 900, 900),
                                                token(" benven", 1200, 1500), token("uti", 1500, 1800), token(".", 1800, 1800),
                                                token("[_TT_100]", 2000, 2000)}}},
                        // A segment without tokens: words spread over its time.
                        QJsonObject{{QStringLiteral("text"), QStringLiteral(" Eccoci qui")},
                                    {QStringLiteral("offsets"), QJsonObject{{QStringLiteral("from"), 3000}, {QStringLiteral("to"), 4000}}}}}}};
        const ai::Transcript transcript = ai::parseWhisperJson(json, 10000);
        QCOMPARE(transcript.language, QStringLiteral("it"));
        QCOMPARE(transcript.words.size(), 6u);
        QCOMPARE(transcript.words[2].text, QStringLiteral("tutti,"));
        QCOMPARE(transcript.words[2].from, 10500);
        QCOMPARE(transcript.words[2].to, 10900);
        QCOMPARE(transcript.words[3].text, QStringLiteral("benvenuti."));
        QCOMPARE(transcript.words[4].text, QStringLiteral("Eccoci"));
        QCOMPARE(transcript.words[4].from, 13000);
        QCOMPARE(transcript.words[5].to, 14000);
        // Kept in the cache and read back.
        QCOMPARE(ai::Transcript::fromJson(QJsonDocument::fromJson(QJsonDocument(transcript.toJson()).toJson()).object()), transcript);
        QVERIFY(!ai::Transcript::fromJson(QJsonObject{}).has_value());
    }

    void captionLinesFollowSentencesAndPauses()
    {
        ai::Transcript transcript;
        const auto word = [&transcript](const char *text, int from, int to) {
            transcript.words.push_back(ai::Transcript::Word{QString::fromUtf8(text), from, to});
        };
        word("Ciao", 0, 300);
        word("a", 300, 400);
        word("tutti.", 400, 800);          // a sentence ends
        word("Oggi", 900, 1200);
        word("parliamo", 1200, 1700);
        word("di", 3000, 3100);            // after a pause of 1.3 s
        word("video", 3100, 3500);
        word("ehm", 3600, 3900);
        const auto toTimeline = [](std::int64_t ms) { return RationalTime(ms * 30 / 1000, Rational(30)); };
        const std::vector<captions::CaptionLine> lines = ai::captionLines(transcript, 0, 5000, toTimeline);
        QCOMPARE(lines.size(), 3u);
        QCOMPARE(lines[0].text, QStringLiteral("Ciao a tutti."));
        QCOMPARE(lines[0].end, toTimeline(900)); // until the next line
        QCOMPARE(lines[1].text, QStringLiteral("Oggi parliamo"));
        QCOMPARE(lines[1].end, toTimeline(2500)); // 0.8 s after its last word, before the pause ends
        QCOMPARE(lines[2].words.size(), 3u);
        QCOMPARE(lines[2].words[0].start, toTimeline(3000));
        // Only the part of the file a clip plays.
        QCOMPARE(ai::captionLines(transcript, 1000, 3200, toTimeline).size(), 2u);
        // Bilingual: the English words said during each line become its translation (one said in the pause goes to
        // the nearest line; one outside the clip's part is left out).
        std::vector<captions::CaptionLine> bilingual = lines;
        ai::Transcript english;
        english.words = {{u"Hi"_s, 0, 300}, {u"everyone."_s, 300, 800}, {u"Today"_s, 900, 1300},
                         {u"we"_s, 1300, 1500}, {u"talk"_s, 2500, 2700}, {u"about"_s, 3000, 3300},
                         {u"video."_s, 3300, 3600}, {u"later"_s, 6000, 6400}};
        ai::attachTranslation(bilingual, english, 0, 5000, toTimeline);
        QCOMPARE(bilingual[0].translation, u"Hi everyone."_s);
        QCOMPARE(bilingual[1].translation, u"Today we talk"_s);
        QCOMPARE(bilingual[2].translation, u"about video."_s);
        // Filler words.
        QVERIFY(ai::isFillerWord(QStringLiteral("ehm")));
        QVERIFY(ai::isFillerWord(QStringLiteral("Uhmm,")));
        QVERIFY(ai::isFillerWord(QStringLiteral("um")));
        QVERIFY(!ai::isFillerWord(QStringLiteral("e")));
        QVERIFY(!ai::isFillerWord(QStringLiteral("umano")));
        QVERIFY(!ai::isFillerWord(QStringLiteral("video")));
    }

    void chaptersStartAtSentencesAfterPauses()
    {
        // Three minutes: a sentence of four words every 10 s, with a long pause before the ones at 62 s and 122 s.
        std::vector<ai::SpokenWord> words;
        for (int sentence = 0; sentence < 18; ++sentence) {
            const std::int64_t start = sentence * 10'000 + (sentence == 6 ? 2'000 : 0) + (sentence == 12 ? 2'000 : 0);
            const QStringList texts{QStringLiteral("parte %1").arg(sentence), QStringLiteral("del"), QStringLiteral("video"),
                                    QStringLiteral("qui.")};
            for (int w = 0; w < texts.size(); ++w) {
                words.push_back(ai::SpokenWord{texts[w], start + w * 500, start + w * 500 + 400});
            }
        }
        const std::vector<ai::Chapter> chapters = ai::findChapters(words, 180'000);
        QCOMPARE(chapters.size(), 3u);
        QCOMPARE(chapters[0].start, 0);
        QCOMPARE(chapters[1].start, 62'000);
        QCOMPARE(chapters[2].start, 122'000);
        QCOMPARE(chapters[1].title, QStringLiteral("Parte 6 del video qui"));
        QCOMPARE(ai::chapterList(chapters, 180'000),
                 QStringLiteral("00:00 Parte 0 del video qui\n01:02 Parte 6 del video qui\n02:02 Parte 12 del video qui"));
        // Too short for YouTube.
        QVERIFY(ai::findChapters(words, 25'000).empty());
    }

    void scriptWordsTakeTheSpokenTimes()
    {
        // Heard (with the recogniser's mistakes): "ciao a tutti ben venuti nel video".
        std::vector<ai::SpokenWord> spoken;
        int t = 0;
        for (const char *word : {"ciao", "a", "tutti", "ben", "venuti", "nel", "video"}) {
            spoken.push_back(ai::SpokenWord{QString::fromUtf8(word), t, t + 300});
            t += 400;
        }
        const std::vector<ai::SpokenWord> aligned = ai::alignScript(QStringLiteral("Ciao a tutti, benvenuti nel mio video!"), spoken);
        QCOMPARE(aligned.size(), 7u);
        QCOMPARE(aligned[0].text, QStringLiteral("Ciao"));
        QCOMPARE(aligned[0].from, 0);
        QCOMPARE(aligned[2].text, QStringLiteral("tutti,"));
        QCOMPARE(aligned[2].from, 800);
        // "mio" was not said: between "nel" and "video".
        QCOMPARE(aligned[5].text, QStringLiteral("mio"));
        QVERIFY(aligned[5].from >= aligned[4].to && aligned[5].to <= aligned[6].from);
        QCOMPARE(aligned[6].text, QStringLiteral("video!"));
        QCOMPARE(aligned[6].from, 2400);
        // Times always go forwards.
        for (size_t i = 1; i < aligned.size(); ++i) {
            QVERIFY(aligned[i].from >= aligned[i - 1].from);
        }
    }

    void montageCutsTheBestMomentsOnTheBeat()
    {
        // 15 videos of 6 s, sharp and lively only between 2 s and 4 s (dark and blurred elsewhere), and 5 photos.
        std::vector<ai::MontageSource> sources;
        for (int v = 0; v < 15; ++v) {
            ai::MontageSource video;
            video.seconds = 6.0;
            for (int k = 0; k < 24; ++k) {
                const double t = k * 0.25;
                const bool good = t >= 2.0 && t < 4.0;
                video.samples.push_back(ai::ShotSample{t, good ? 0.9f : 0.1f, good ? 0.5f : 0.05f, good ? 0.05f : 0.0f});
            }
            sources.push_back(video);
        }
        for (int p = 0; p < 5; ++p) {
            sources.push_back(ai::MontageSource{true, 0.0, {}});
        }
        std::vector<double> beats; // 120 BPM
        for (int b = 1; b < 200; ++b) {
            beats.push_back(b * 0.5);
        }
        const ai::MontageStyle &vlog = *ai::montageStyle(QStringLiteral("vlog"));
        const std::vector<ai::MontagePiece> plan = ai::planMontage(sources, beats, vlog, 30.0, 1);
        double time = 0.0;
        for (const ai::MontagePiece &piece : plan) {
            QVERIFY(piece.length >= 0.99 && piece.length <= 2.01); // 2–4 beats
            if (!sources[static_cast<size_t>(piece.source)].photo) {
                QVERIFY2(piece.from >= 1.5 && piece.from + piece.length <= 4.5, qPrintable(QString::number(piece.from)));
            }
            time += piece.length;
            // Every cut on a beat.
            QVERIFY2(std::abs(time * 2.0 - std::round(time * 2.0)) < 1e-6, qPrintable(QString::number(time)));
        }
        QVERIFY2(time >= 29.5 && time <= 30.01, qPrintable(QString::number(time)));
        // Another seed, another montage; the same seed, the same one.
        QVERIFY(ai::planMontage(sources, beats, vlog, 30.0, 2) != plan);
        QCOMPARE(ai::planMontage(sources, beats, vlog, 30.0, 1), plan);
        // Free length: every file once, in order with seed 0.
        const std::vector<ai::MontagePiece> all = ai::planMontage(sources, {}, vlog, 0.0, 0);
        QCOMPARE(all.size(), sources.size());
        QCOMPARE(all.front().source, 0);
        QCOMPARE(all.front().length, 2.0); // the style's seconds without music
    }

    void scriptBecomesScenes()
    {
        const QString twelve = QStringLiteral("uno due tre quattro cinque sei sette otto nove dieci undici dodici.");
        const QString script = QStringLiteral("Benvenuti nel video.\n\n  \n") + twelve + u' ' + twelve + u' ' + twelve + u"\n\n"_s;
        const QStringList scenes = ai::splitScript(script);
        QCOMPARE(scenes.size(), 3);
        QCOMPARE(scenes[0], QStringLiteral("Benvenuti nel video."));
        QCOMPARE(captions::splitWords(scenes[1]).size(), 24); // two sentences fit in 30 words
        QCOMPARE(captions::splitWords(scenes[2]).size(), 12);
        QVERIFY(ai::splitScript(QStringLiteral("  \n\n ")).isEmpty());
        QCOMPARE(ai::readingSeconds(QStringLiteral("Ciao")), 2.0);
        QVERIFY(std::abs(ai::readingSeconds(twelve + u' ' + twelve) - (24 / 2.6 + 0.4)) < 1e-9);
    }

    void highlightsAreTheLivelyMoments()
    {
        // Two minutes of talk at −30 dB with a short pause every 8 s, louder (laughter, cheering) at 40–50 s and 90–96 s.
        ai::HighlightInput input;
        input.seconds = 120.0;
        for (int k = 0; k < 12000; ++k) {
            const double t = k / 100.0;
            float level = std::fmod(t, 8.0) < 0.8 ? -60.0f : -30.0f;
            if ((t >= 40.0 && t < 50.0) || (t >= 90.0 && t < 96.0)) {
                level = -12.0f;
            }
            input.levels.push_back(level);
        }
        input.cuts = {20.0, 60.0, 100.0};
        for (double t = 40.0; t < 50.0; t += 0.3) {
            input.words.emplace_back(t, t + 0.25); // and a lot is said there
        }
        const std::vector<ai::Span> segments = ai::highlightSegments(input);
        for (const ai::Span &segment : segments) {
            QVERIFY(segment.length() >= 2.99 && segment.length() <= 15.01);
        }
        const std::vector<ai::Span> best = ai::findHighlights(input, 15.0);
        double total = 0.0;
        for (const ai::Span &span : best) {
            total += span.length();
            // Mostly lively: at least 40 % of the piece in one of the lively parts.
            const double lively = std::max(0.0, std::min(span.to, 50.0) - std::max(span.from, 40.0)) +
                                  std::max(0.0, std::min(span.to, 96.0) - std::max(span.from, 90.0));
            QVERIFY2(lively >= 0.4 * span.length(), qPrintable(QStringLiteral("%1–%2").arg(span.from).arg(span.to)));
        }
        QVERIFY(total >= 15.0 && total < 30.0);
        for (size_t i = 1; i < best.size(); ++i) {
            QVERIFY(best[i].from >= best[i - 1].to - 1e-9); // in their order in the video
        }
        // Two short clips of about 20 s: around the two lively moments, not overlapping.
        const std::vector<ai::Span> clips = ai::findShortClips(input, 2, 20.0);
        QCOMPARE(clips.size(), 2u);
        QVERIFY(clips[0].from <= 45.0 && clips[0].to >= 45.0); // the liveliest first
        QVERIFY(clips[1].from <= 93.0 && clips[1].to >= 93.0);
        QVERIFY(clips[0].length() >= 15.0 && clips[0].length() <= 60.0);
    }
};

QTEST_GUILESS_MAIN(TestAi)
#include "tst_ai.moc"
