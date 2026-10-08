// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptionsController.h"

#include "core/edit/TimelineEditor.h"
#include "core/project/Captions.h"
#include "core/serialization/ProjectJson.h"
#include "core/subtitle/SubtitleFormat.h"
#include "engine/playback/TimelinePlayer.h"
#include "fx/Library.h"
#include "ui/controllers/EditorController.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace velacut::ui {

namespace {

// Longest line added by hand.
constexpr int kNewLineSeconds = 2;

QString clock(const RationalTime &time)
{
    const std::int64_t tenths = std::max<std::int64_t>(0, time.rescaled(Rational(10), Rounding::NearestEven).value());
    return u"%1:%2.%3"_s.arg(tenths / 600).arg(tenths / 10 % 60, 2, 10, u'0').arg(tenths % 10);
}

QColor toQColor(const Color &c)
{
    return QColor(c.r, c.g, c.b, c.a);
}

Color fromQColor(const QColor &c)
{
    return Color{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                 static_cast<std::uint8_t>(c.blue()), static_cast<std::uint8_t>(c.alpha())};
}

Color colorOf(const Param &param, Color fallback)
{
    const ParamValue value = param.staticValue();
    return std::holds_alternative<Color>(value) ? std::get<Color>(value) : fallback;
}

double numberOf(const Param &param, double fallback)
{
    const ParamValue value = param.staticValue();
    return std::holds_alternative<double>(value) ? std::get<double>(value) : fallback;
}

// A text file in UTF-8 (with or without byte order mark), else in Latin-1 (older subtitle files).
QString readText(const QByteArray &bytes)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(bytes);
    if (utf8.hasError()) {
        text = QString::fromLatin1(bytes);
    }
    return text;
}

} // namespace

CaptionsController::CaptionsController(EditorController &editor)
    : QObject(&editor)
    , m_editor(editor)
{
    connect(&editor, &EditorController::modelChanged, this, [this] {
        emit changed();
        updateCurrent();
    });
    connect(&editor, &EditorController::selectionChanged, this, [this] {
        emit changed(); // the caption track followed is the one of the selected line
        updateCurrent();
    });
    connect(editor.player(), &engine::TimelinePlayer::positionChanged, this, &CaptionsController::updateCurrent);
}

const Track *CaptionsController::track() const
{
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return nullptr;
    }
    const std::optional<ClipId> focus = m_editor.focusClip();
    const Track *first = nullptr;
    for (const Track &candidate : sequence->visualTracks) {
        if (!candidate.captions) {
            continue;
        }
        if (focus && candidate.findClip(*focus)) {
            return &candidate;
        }
        first = first ? first : &candidate;
    }
    return first;
}

const Clip *CaptionsController::line(int index) const
{
    const Track *captions = track();
    if (!captions || index < 0 || index >= static_cast<int>(captions->clips.size())) {
        return nullptr;
    }
    return &captions->clips[static_cast<size_t>(index)];
}

bool CaptionsController::hasCaptions() const
{
    const Track *captions = track();
    return captions && !captions->clips.empty();
}

bool CaptionsController::locked() const
{
    const Track *captions = track();
    return captions && captions->locked;
}

QVariantList CaptionsController::lines() const
{
    QVariantList result;
    const Track *captions = track();
    if (!captions) {
        return result;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    for (const Clip &clip : captions->clips) {
        const SubtitleClipData *data = clip.subtitle();
        if (!data) {
            continue;
        }
        result << QVariantMap{{u"clipId"_s, clip.id.toString()},
                              {u"start"_s, qint64(clip.start.rescaled(rate, Rounding::NearestEven).value())},
                              {u"end"_s, qint64(clip.end().rescaled(rate, Rounding::NearestEven).value())},
                              {u"time"_s, clock(clip.start)},
                              {u"text"_s, data->text},
                              {u"translation"_s, data->translation}};
    }
    return result;
}

void CaptionsController::updateCurrent()
{
    int current = -1;
    if (const Track *captions = track()) {
        const RationalTime playhead(m_editor.player()->position(), m_editor.data().settings.frameRate);
        for (size_t i = 0; i < captions->clips.size() && captions->clips[i].start <= playhead; ++i) {
            current = static_cast<int>(i);
        }
    }
    if (current != m_current) {
        m_current = current;
        emit currentChanged();
    }
}

CaptionStyle CaptionsController::currentStyle() const
{
    if (const Track *captions = track()) {
        return projectjson::captionStyleOf(*captions);
    }
    if (m_nextStyle) {
        return *m_nextStyle;
    }
    return projectjson::captionStyleOf(Track{});
}

QString CaptionsController::styleId() const
{
    return currentStyle().preset;
}

QVariantMap CaptionsController::style() const
{
    const CaptionStyle style = currentStyle();
    const TextStyle &text = style.text;
    return {{u"wordsPerLine"_s, style.maxWordsPerLine},
            {u"position"_s, style.position},
            {u"highlight"_s, static_cast<int>(style.highlight)},
            {u"highlightColor"_s, toQColor(style.highlightColor)},
            {u"animation"_s, static_cast<int>(style.animation)},
            {u"uppercase"_s, style.uppercase},
            {u"size"_s, numberOf(text.size, 0.055)},
            {u"color"_s, toQColor(colorOf(text.color, Color{255, 255, 255, 255}))},
            {u"bold"_s, text.fontWeight >= 700},
            {u"stroke"_s, text.stroke.has_value()},
            {u"background"_s, text.background.has_value()},
            {u"backgroundColor"_s, toQColor(text.background ? text.background->color : TextBackground{}.color)}};
}

bool CaptionsController::importFile(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        emit m_editor.message(tr("The file %1 cannot be read.").arg(QFileInfo(path).fileName()), false);
        return false;
    }
    const auto entries = SubtitleFormat::parse(readText(input.readAll()), m_editor.data().settings.frameRate);
    const std::vector<captions::CaptionLine> lines = entries ? captions::linesOf(*entries) : std::vector<captions::CaptionLine>{};
    if (lines.empty()) {
        emit m_editor.message(tr("%1 has no captions velacut can read (SRT or WebVTT).").arg(QFileInfo(path).fileName()), false);
        return false;
    }
    const std::optional<CaptionStyle> style = hasCaptions() ? std::nullopt : m_nextStyle;
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).insertCaptions(lines, style);
    const ClipId first = result.primaryClip;
    if (!m_editor.push(std::move(result))) {
        return false;
    }
    m_editor.select(first.toString(), false);
    emit m_editor.message(tr("%n caption line(s) added", nullptr, static_cast<int>(lines.size())), true);
    return true;
}

QString CaptionsController::exportFileName() const
{
    QString name = m_editor.name().trimmed();
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    return (name.isEmpty() ? tr("Captions") : name) + u".srt"_s;
}

QUrl CaptionsController::exportUrl() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + u'/' + exportFileName());
}

bool CaptionsController::exportFile(const QUrl &file)
{
    const Track *captions = track();
    if (!captions || captions->clips.empty()) {
        emit m_editor.message(tr("There are no captions to save yet."), false);
        return false;
    }
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != u"srt"_s && suffix != u"vtt"_s) {
        path += u".srt"_s;
    }
    const std::vector<SubtitleEntry> entries = captions::entriesOf(*captions);
    const QString text = path.endsWith(u".vtt"_s, Qt::CaseInsensitive) ? SubtitleFormat::formatVTT(entries)
                                                                        : SubtitleFormat::formatSRT(entries);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(text.toUtf8()) < 0 || !output.commit()) {
        emit m_editor.message(tr("The captions cannot be saved in %1.").arg(path), false);
        return false;
    }
    emit m_editor.message(tr("Captions saved: %1").arg(QFileInfo(path).fileName()), false);
    return true;
}

void CaptionsController::previewStyle(const QString &presetId)
{
    const Track *captions = track();
    const fx::CaptionStylePreset *preset = fx::Library::core().captionStyle(presetId);
    if (!captions || !preset) {
        return;
    }
    engine::TimelineProjection::Preview preview;
    preview.captionTrack = captions->id;
    preview.captionStyle = projectjson::captionStyleFromJson(preset->style);
    preview.captionStyle->preset = presetId;
    m_editor.player()->setPreview(std::move(preview));
}

void CaptionsController::clearPreview()
{
    m_editor.player()->clearPreview();
}

bool CaptionsController::applyStyle(const QString &presetId)
{
    const fx::CaptionStylePreset *preset = fx::Library::core().captionStyle(presetId);
    if (!preset) {
        return false;
    }
    clearPreview();
    CaptionStyle style = projectjson::captionStyleFromJson(preset->style);
    style.preset = presetId;
    if (!track()) {
        // No captions yet: the style of the ones that come next.
        m_nextStyle = style;
        emit changed();
        emit m_editor.message(tr("Style chosen: your captions will have it."), false);
        return true;
    }
    return setStyle(style, tr("Apply caption style"), false);
}

bool CaptionsController::setStyle(const CaptionStyle &style, const QString &undoText, bool gesture)
{
    const Track *captions = track();
    if (!captions) {
        m_nextStyle = style;
        emit changed();
        return true;
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .updateTrack(captions->id, [&style](Track &t) { projectjson::setCaptionStyle(t, style); }, undoText);
    return m_editor.push(std::move(result), gesture ? MergeKey{u"captions:style"_s, m_gesture} : MergeKey{});
}

bool CaptionsController::setStyleValue(const QString &key, const QVariant &value)
{
    CaptionStyle style = currentStyle();
    TextStyle &text = style.text;
    if (key == u"wordsPerLine"_s) {
        style.maxWordsPerLine = std::clamp(value.toInt(), 0, 20);
    } else if (key == u"position"_s) {
        style.position = std::clamp(value.toDouble(), -0.5, 0.5);
    } else if (key == u"highlight"_s) {
        style.highlight = static_cast<CaptionHighlight>(std::clamp(value.toInt(), 0, static_cast<int>(CaptionHighlight::Karaoke)));
    } else if (key == u"highlightColor"_s) {
        style.highlightColor = fromQColor(value.value<QColor>());
    } else if (key == u"animation"_s) {
        style.animation = static_cast<CaptionAnimation>(std::clamp(value.toInt(), 0, static_cast<int>(CaptionAnimation::Bounce)));
    } else if (key == u"uppercase"_s) {
        style.uppercase = value.toBool();
    } else if (key == u"size"_s) {
        text.size = Param(std::clamp(value.toDouble(), 0.02, 0.15));
    } else if (key == u"color"_s) {
        text.color = Param(fromQColor(value.value<QColor>()));
    } else if (key == u"bold"_s) {
        text.fontWeight = value.toBool() ? 800 : 500;
    } else if (key == u"stroke"_s) {
        text.stroke = value.toBool() ? std::optional(TextStroke{Param(Color{0, 0, 0, 255}), 0.07}) : std::nullopt;
    } else if (key == u"background"_s) {
        if (value.toBool()) {
            TextBackground background;
            background.color = Color{0, 0, 0, 200};
            background.padding = 0.35;
            background.radius = 0.18;
            text.background = background;
        } else {
            text.background.reset();
        }
    } else if (key == u"backgroundColor"_s) {
        if (!text.background) {
            text.background = TextBackground{};
        }
        text.background->color = fromQColor(value.value<QColor>());
    } else {
        return false;
    }
    return setStyle(style, tr("Change captions"), true);
}

void CaptionsController::endGesture()
{
    ++m_gesture;
}

bool CaptionsController::setLineText(int index, const QString &text)
{
    const Clip *clip = line(index);
    if (!clip || clip->subtitle()->text == captions::splitWords(text).join(u' ')) {
        return false;
    }
    if (text.trimmed().isEmpty()) {
        return deleteLine(index);
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .updateClips({clip->id}, [&text](Clip &c) { c.payload = captions::withText(*c.subtitle(), text); },
                                         tr("Edit caption"));
    return m_editor.push(std::move(result));
}

bool CaptionsController::setLineTranslation(int index, const QString &text)
{
    const Clip *clip = line(index);
    const QString translation = text.simplified();
    if (!clip || clip->subtitle()->translation == translation) {
        return false;
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .updateClips({clip->id},
                                         [&translation](Clip &c) {
                                             SubtitleClipData data = *c.subtitle();
                                             data.translation = translation;
                                             c.payload = std::move(data);
                                         },
                                         tr("Edit translation"));
    return m_editor.push(std::move(result));
}

void CaptionsController::seekToLine(int index)
{
    if (const Clip *clip = line(index)) {
        m_editor.player()->seek(static_cast<int>(clip->start.rescaled(m_editor.data().settings.frameRate, Rounding::NearestEven).value()));
        m_editor.select(clip->id.toString(), false);
    }
}

bool CaptionsController::splitLine(int index)
{
    const Clip *clip = line(index);
    if (!clip) {
        return false;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    RationalTime at(m_editor.player()->position(), rate);
    if (!(at > clip->start && at < clip->end())) {
        // The playhead is elsewhere: in the middle of the line.
        at = clip->start + RationalTime(clip->duration.rescaled(rate, Rounding::NearestEven).value() / 2, rate);
    }
    return m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).splitClip(clip->id, at));
}

bool CaptionsController::joinWithNext(int index)
{
    const Clip *clip = line(index);
    return clip && m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).mergeCaptionLines(clip->id));
}

bool CaptionsController::deleteLine(int index)
{
    const Clip *clip = line(index);
    if (!clip) {
        return false;
    }
    if (const Track *captions = track(); captions && captions->clips.size() == 1) {
        m_nextStyle = projectjson::captionStyleOf(*captions); // the track goes with its last line: keep its look
    }
    return m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).deleteClips({clip->id}));
}

bool CaptionsController::addLine()
{
    const Rational rate = m_editor.data().settings.frameRate;
    const RationalTime at(m_editor.player()->position(), rate);
    RationalTime end = at + RationalTime::fromSeconds(Rational(kNewLineSeconds), rate, Rounding::NearestEven);
    if (const Track *captions = track()) {
        for (const Clip &clip : captions->clips) {
            if (clip.start <= at && clip.end() > at) {
                emit m_editor.message(tr("There is already a caption here: move the playhead to a moment without one."), false);
                return false;
            }
            if (clip.start > at) {
                end = std::min(end, clip.start);
            }
        }
    }
    const std::optional<CaptionStyle> style = track() ? std::nullopt : m_nextStyle;
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .insertCaptions({{at, end, tr("New caption"), {}}}, style);
    const ClipId added = result.primaryClip;
    if (!m_editor.push(std::move(result))) {
        return false;
    }
    m_editor.select(added.toString(), false);
    return true;
}

bool CaptionsController::shiftAll(double seconds)
{
    const Track *captions = track();
    if (!captions) {
        return false;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    const RationalTime delta = RationalTime::fromSeconds(Rational(std::llround(seconds * 1000.0), 1000), rate,
                                                         Rounding::NearestEven);
    return m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).shiftCaptions(captions->id, delta),
                         MergeKey{u"captions:shift"_s, m_gesture});
}

int CaptionsController::countMatches(const QString &text) const
{
    const Track *captions = track();
    if (!captions || text.trimmed().isEmpty()) {
        return 0;
    }
    return static_cast<int>(std::count_if(captions->clips.begin(), captions->clips.end(), [&text](const Clip &clip) {
        return clip.subtitle() && clip.subtitle()->text.contains(text, Qt::CaseInsensitive);
    }));
}

int CaptionsController::replaceAll(const QString &text, const QString &replacement)
{
    const Track *captions = track();
    if (!captions || text.trimmed().isEmpty()) {
        return 0;
    }
    std::vector<ClipId> matching;
    for (const Clip &clip : captions->clips) {
        if (clip.subtitle() && clip.subtitle()->text.contains(text, Qt::CaseInsensitive)) {
            matching.push_back(clip.id);
        }
    }
    if (matching.empty()) {
        return 0;
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .updateClips(matching,
                                         [&](Clip &c) {
                                             QString changed = c.subtitle()->text;
                                             changed.replace(text, replacement, Qt::CaseInsensitive);
                                             c.payload = captions::withText(*c.subtitle(), changed);
                                         },
                                         tr("Replace in captions"));
    if (!m_editor.push(std::move(result))) {
        return 0;
    }
    const int count = static_cast<int>(matching.size());
    emit m_editor.message(tr("Replaced in %n line(s)", nullptr, count), true);
    return count;
}

} // namespace velacut::ui
