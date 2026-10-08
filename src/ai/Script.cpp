// SPDX-License-Identifier: GPL-3.0-or-later
#include "Script.h"

#include "core/project/Captions.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace velacut::ai {

QStringList splitScript(const QString &script, int maxWords)
{
    static const QRegularExpression paragraphs(u"\n\\s*\n"_s);
    static const QRegularExpression sentenceEnd(u"[.!?\u2026][\"'\u201D\u00BB)]*$"_s);
    QStringList scenes;
    for (const QString &paragraph : script.split(paragraphs, Qt::SkipEmptyParts)) {
        const QStringList words = captions::splitWords(paragraph);
        if (words.isEmpty()) {
            continue;
        }
        // Sentences, put together up to maxWords.
        QStringList scene;
        QStringList sentence;
        const auto flushSentence = [&] {
            if (!scene.isEmpty() && scene.size() + sentence.size() > maxWords) {
                scenes << scene.join(u' ');
                scene.clear();
            }
            scene += sentence;
            sentence.clear();
        };
        for (const QString &word : words) {
            sentence << word;
            if (sentenceEnd.match(word).hasMatch()) {
                flushSentence();
            } else if (sentence.size() >= maxWords) {
                flushSentence(); // a very long sentence: cut it anyway
            }
        }
        flushSentence();
        if (!scene.isEmpty()) {
            scenes << scene.join(u' ');
        }
    }
    return scenes;
}

double readingSeconds(const QString &text)
{
    return std::max(2.0, static_cast<double>(captions::splitWords(text).size()) / 2.6 + 0.4);
}

} // namespace velacut::ai
