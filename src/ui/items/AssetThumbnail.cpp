// SPDX-License-Identifier: GPL-3.0-or-later
#include "AssetThumbnail.h"

#include "controllers/EditorController.h"
#include "core/serialization/ProjectJson.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/mlt/Services.h"
#include "engine/text/CaptionRenderer.h"
#include "engine/text/TextRenderer.h"
#include "fx/Animation.h"
#include "fx/AudioVisualizer.h"
#include "fx/Color.h"
#include "fx/Library.h"
#include "fx/Transition.h"
#include "fx/VideoEffect.h"
#include "models/AssetLibraryModel.h"

#include <QCache>
#include <QLinearGradient>
#include <QPainter>
#include <QQuickWindow>
#include <QRadialGradient>
#include <QThreadPool>

#include <cmath>

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {

// Pictures by key (GUI thread only): a library shows a few dozen items, each a few KiB.
QCache<QString, QImage> &cache()
{
    static QCache<QString, QImage> images(8 * 1024); // KiB
    return images;
}

fx::ImageView viewOf(QImage &image)
{
    return {image.bits(), image.width(), image.height(), static_cast<int>(image.bytesPerLine())};
}

// A sample scene (content colours, not the theme's): sky, sun and ground, warm (A) or cool (B), so looks and the
// shapes of transitions read at a glance.
QImage scene(QSize size, bool warm)
{
    QImage image(size, QImage::Format_RGBA8888);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, size.height());
    sky.setColorAt(0, warm ? QColor(0xF5, 0x9E, 0x5B) : QColor(0x3A, 0x6E, 0xC9));
    sky.setColorAt(1, warm ? QColor(0xFB, 0xD8, 0x8E) : QColor(0x9F, 0xD3, 0xF0));
    painter.fillRect(image.rect(), sky);
    const double w = size.width();
    const double h = size.height();
    QRadialGradient sun(QPointF(w * 0.7, h * 0.38), h * 0.2);
    sun.setColorAt(0, warm ? QColor(0xFF, 0xF4, 0xD6) : QColor(0xFF, 0xFF, 0xFF));
    sun.setColorAt(1, warm ? QColor(0xFF, 0xC8, 0x6B) : QColor(0xE3, 0xF2, 0xFD));
    painter.setPen(Qt::NoPen);
    painter.setBrush(sun);
    painter.drawEllipse(QPointF(w * 0.7, h * 0.38), h * 0.16, h * 0.16);
    painter.setBrush(warm ? QColor(0x6D, 0x4C, 0x41) : QColor(0x2E, 0x7D, 0x32));
    QPolygonF hills;
    hills << QPointF(0, h) << QPointF(0, h * 0.72) << QPointF(w * 0.3, h * 0.6) << QPointF(w * 0.55, h * 0.74)
          << QPointF(w * 0.8, h * 0.62) << QPointF(w, h * 0.7) << QPointF(w, h);
    painter.drawPolygon(hills);
    return image;
}

// `source` scaled to cover `size`, centred.
QImage cover(const QImage &source, QSize size)
{
    const QImage scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return scaled.copy((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2, size.width(), size.height())
        .convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

AssetThumbnail::AssetThumbnail(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
}

void AssetThumbnail::setEditor(EditorController *editor)
{
    if (editor == m_editor) {
        return;
    }
    disconnect(m_selectionConnection);
    m_editor = editor;
    if (m_editor) {
        // Filters show the clip they would apply to: another selection, another picture.
        m_selectionConnection = connect(m_editor, &EditorController::selectionChanged, this, [this] {
            if (m_kind == AssetLibraryModel::Filters || m_kind == AssetLibraryModel::VideoEffects) {
                request();
            }
        });
    }
    emit editorChanged();
    request();
}

void AssetThumbnail::setKind(int kind)
{
    if (kind != m_kind) {
        m_kind = kind;
        emit assetChanged();
        request();
    }
}

void AssetThumbnail::setAssetId(const QString &assetId)
{
    if (assetId != m_assetId) {
        m_assetId = assetId;
        emit assetChanged();
        request();
    }
}

void AssetThumbnail::setProgress(double progress)
{
    progress = std::clamp(progress, 0.0, 1.0);
    if (progress != m_progress) {
        m_progress = progress;
        emit progressChanged();
        request();
    }
}

QImage AssetThumbnail::sample(QString *key) const
{
    if ((m_kind != AssetLibraryModel::Filters && m_kind != AssetLibraryModel::VideoEffects) || !m_editor) {
        return {};
    }
    const std::optional<ClipId> clipId = m_editor->clipForLibrary();
    const Clip *clip = clipId ? m_editor->data().findClip(*clipId) : nullptr;
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? m_editor->data().findMedia(media->mediaId) : nullptr;
    if (!source || source->kind == MediaKind::Audio) {
        return {};
    }
    const QImage strip = m_editor->analysis().thumbnails(*source);
    const int count = engine::MediaAnalysis::thumbnailCount(*source);
    if (strip.isNull() || count < 1) {
        return {};
    }
    // The thumbnail nearest to the middle of the clip.
    const double seconds = source->info.duration ? source->info.duration->toSecondsDouble() : 0.0;
    const double middle = media->sourceIn.toSecondsDouble() + clip->duration.toSecondsDouble() * media->speed / 2;
    const int index = seconds > 0 ? std::clamp(static_cast<int>(middle / seconds * count), 0, count - 1) : 0;
    const int width = strip.width() / count;
    *key = source->fingerprint.value + u'@' + QString::number(index);
    return strip.copy(index * width, 0, width, strip.height());
}

void AssetThumbnail::request()
{
    if (m_assetId.isEmpty() || width() <= 0 || height() <= 0) {
        return;
    }
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize size(static_cast<int>(std::ceil(width() * ratio)), static_cast<int>(std::ceil(height() * ratio)));
    QString sampleKey;
    const QImage frame = sample(&sampleKey);
    // Transitions and animations are drawn at a few steps of progress: smooth enough for the hover animation, cached.
    const bool animated = m_kind == AssetLibraryModel::Transitions || m_kind == AssetLibraryModel::Animations ||
                          m_kind == AssetLibraryModel::Stickers || m_kind == AssetLibraryModel::VideoEffects ||
                          m_kind == AssetLibraryModel::CaptionStyles;
    const double progress = animated ? std::round(m_progress * 24) / 24 : 0.0;
    const QString key = u"%1|%2|%3|%4|%5x%6"_s.arg(m_kind).arg(m_assetId, sampleKey).arg(progress).arg(size.width()).arg(size.height());
    if (const QImage *cached = cache().object(key)) {
        m_image = *cached;
        update();
        return;
    }
    if (m_pending == key) {
        return;
    }
    m_pending = key;
    const int kind = m_kind;
    const QString assetId = m_assetId;
    QPointer<AssetThumbnail> self(this);
    QThreadPool::globalInstance()->start([self, key, kind, assetId, progress, frame, size] {
        const QImage image = render(kind, assetId, progress, frame, size);
        QMetaObject::invokeMethod(qApp, [self, key, image] {
            cache().insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
            if (self && self->m_pending == key) {
                self->m_pending.clear();
                self->m_image = image;
                self->update();
            }
        });
    });
}

QImage AssetThumbnail::render(int kind, const QString &assetId, double progress, const QImage &sample, QSize size)
{
    const fx::Library &library = fx::Library::core();
    if (kind == AssetLibraryModel::Filters) {
        QImage image = sample.isNull() ? scene(size, true) : cover(sample, size);
        if (const fx::FilterPreset *preset = library.filter(assetId)) {
            fx::ColorLut(preset->look).apply(viewOf(image));
            if (preset->vignette != 0) {
                fx::vignette(viewOf(image), preset->vignette, 0.5, 0, image.height());
            }
        }
        return image;
    }
    if (kind == AssetLibraryModel::CaptionStyles) {
        // Three words over a picture, said one after the other over `progress` (the middle one at rest).
        QImage image = scene(size, true);
        const fx::CaptionStylePreset *preset = library.captionStyle(assetId);
        if (!preset) {
            return image;
        }
        CaptionStyle style = projectjson::captionStyleFromJson(preset->style);
        style.text.size = Param(0.17);
        style.position = 0.0;
        if (style.maxWordsPerLine > 3) {
            style.maxWordsPerLine = 3;
        }
        SubtitleClipData line;
        line.text = tr("Hi everyone here");
        constexpr int length = 90;
        const auto layout = engine::CaptionRenderer::layout(line, style, length, size, Rational(30));
        const auto frame = std::clamp<std::int64_t>(std::llround(progress * (length - 1)), 0, length - 1);
        QPainter painter(&image);
        painter.drawImage(0, 0, engine::CaptionRenderer::render(*layout, frame, size));
        return image;
    }
    if (kind == AssetLibraryModel::Transitions) {
        const QImage a = scene(size, true);
        const QImage b = scene(size, false);
        QImage out(size, QImage::Format_RGBA8888);
        out.fill(Qt::black);
        const fx::TransitionPreset *preset = library.transition(assetId);
        if (!preset) {
            return a;
        }
        fx::TransitionParams params;
        params.softness = preset->params.value(u"softness"_s).toDouble(params.softness);
        fx::renderTransition(preset->kernel, viewOf(out),
                             fx::ConstImageView(a.constBits(), a.width(), a.height(), static_cast<int>(a.bytesPerLine())),
                             fx::ConstImageView(b.constBits(), b.width(), b.height(), static_cast<int>(b.bytesPerLine())),
                             fx::ease(fx::Easing::EaseInOut, progress), params, 0, out.height());
        return out;
    }
    if (kind == AssetLibraryModel::Animations) {
        // The sample scene as a card, placed as the animation places a clip at `progress`.
        QImage image(size, QImage::Format_RGBA8888);
        image.fill(QColor(0x5F, 0x63, 0x68));
        double x = 0, y = 0, scaleX = 1, scaleY = 1, rotation = 0, opacity = 1, cropL = 0, cropT = 0, cropR = 0, cropB = 0;
        const fx::AnimationPreset *preset = library.animation(assetId);
        const QString category = preset ? preset->category : QString();
        if (category == u"out"_s) {
            fx::applyOutAnimation(assetId, progress, x, y, scaleX, scaleY, rotation, opacity, cropL, cropT, cropR, cropB);
        } else if (category == u"loop"_s) {
            fx::applyLoopAnimation(assetId, progress, x, y, scaleX, scaleY, rotation, opacity);
        } else {
            fx::applyInAnimation(assetId, progress, x, y, scaleX, scaleY, rotation, opacity, cropL, cropT, cropR, cropB);
        }
        const QSize cardSize(size.width() * 3 / 5, size.height() * 3 / 5);
        const QImage card = scene(cardSize, true);
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter.setClipRect(image.rect());
        painter.translate(size.width() / 2.0 + x * size.width(), size.height() / 2.0 + y * size.height());
        painter.rotate(rotation);
        painter.scale(scaleX, scaleY);
        painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
        const QRectF target(-cardSize.width() / 2.0, -cardSize.height() / 2.0, cardSize.width(), cardSize.height());
        const QRectF visible(target.left() + cropL * target.width(), target.top() + cropT * target.height(),
                             target.width() * std::max(0.0, 1.0 - cropL - cropR), target.height() * std::max(0.0, 1.0 - cropT - cropB));
        painter.setClipRect(visible, Qt::IntersectClip);
        painter.drawImage(target, card);
        return image;
    }
    if (kind == AssetLibraryModel::VideoEffects) {
        // The clip it would apply to (or the sample scene), with the effect at `progress` of a two-second loop.
        QImage image = sample.isNull() ? scene(size, true) : cover(sample, size);
        const fx::VideoEffectPreset *preset = library.videoEffect(assetId);
        if (!preset) {
            return image;
        }
        if (const std::optional<fx::EffectKernel> kernel = fx::effectKernel(preset->kernel)) {
            fx::VideoEffectParams params = engine::videoEffectParams(preset->params, {});
            params.time = progress * 2.0;
            fx::renderVideoEffect(*kernel, viewOf(image), params);
        } else if (preset->type.startsWith(u"vedit.beat."_s)) {
            // Effects on the beat: a pulse every half second.
            const double pulse = std::exp(-4.0 * std::fmod(progress * 2.0, 0.5) / 0.18);
            QPainter painter(&image);
            painter.setOpacity(std::clamp(pulse * 0.7, 0.0, 1.0));
            painter.fillRect(image.rect(), Qt::white);
        }
        return image;
    }
    if (kind == AssetLibraryModel::Stickers) {
        // On the neutral grey of the text styles; animated pictures and visualizers move with `progress`.
        QImage image(size, QImage::Format_RGBA8888);
        image.fill(QColor(0x5F, 0x63, 0x68));
        const fx::StickerPreset *preset = library.sticker(assetId);
        if (!preset) {
            return image;
        }
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        if (!preset->graphic.isEmpty()) {
            // A three-second element at `progress`: counting, ticking, being drawn.
            const GraphicSettings settings = projectjson::graphicFromJson(preset->graphic);
            const double total = std::max(1.0, preset->defaultDuration);
            painter.drawImage(0, 0, fx::renderGraphic(engine::graphicParams(settings), progress * total, total, size,
                                                      engine::makeGraphicGlyphs(settings, size.height())));
            return image;
        }
        if (!preset->visualizer.isEmpty()) {
            const fx::VisualizerSettings settings = engine::visualizerSettings(projectjson::visualizerFromJson(preset->visualizer));
            painter.drawImage(0, 0, fx::renderAudioVisualizer(settings, fx::exampleVisualizerFrame(progress * 2.0, settings.barCount), size));
            return image;
        }
        const int side = std::min(size.width(), size.height()) * 4 / 5;
        const engine::StickerPicture picture = !preset->path.isEmpty() ? engine::loadStickerPicture(preset->path, side * 2)
                                                                       : engine::renderEmoji(preset->emoji, side * 2);
        if (picture.frames.empty()) {
            return image;
        }
        const size_t index = std::min(picture.frames.size() - 1, static_cast<size_t>(progress * static_cast<double>(picture.frames.size())));
        const QImage &frame = picture.frames[index];
        const QSize fitted = frame.size().scaled(side, side, Qt::KeepAspectRatio);
        painter.drawImage(QRect((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2, fitted.width(),
                                fitted.height()),
                          frame);
        return image;
    }
    // Text style: a word in the style, large enough to see it, on a neutral grey that shows light and dark styles.
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(QColor(0x5F, 0x63, 0x68));
    if (const fx::TextStylePreset *preset = library.textStyle(assetId)) {
        TextClipData text;
        text.text = tr("Text");
        text.style = projectjson::textStyleFromJson(preset->style);
        text.style.size = Param(0.34);
        QPainter painter(&image);
        painter.drawImage(0, 0, engine::TextRenderer::render(text, size));
    }
    return image;
}

void AssetThumbnail::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        request();
    }
}

void AssetThumbnail::paint(QPainter *painter)
{
    if (m_image.isNull()) {
        request();
        return;
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(boundingRect(), m_image);
}

} // namespace vedit::ui
