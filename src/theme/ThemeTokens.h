// SPDX-License-Identifier: GPL-3.0-or-later
// Material 3 design tokens other than colors and type (docs/DESIGN_SYSTEM.md). No component may hard-code
// these values: QML reads them from Theme.shape, Theme.state, Theme.space, Theme.elevation and Theme.motion.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace vedit::theme {

// Corner radius tokens (dp).
class ThemeShape : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(qreal none MEMBER m_none CONSTANT FINAL)
    Q_PROPERTY(qreal extraSmall MEMBER m_extraSmall CONSTANT FINAL)
    Q_PROPERTY(qreal small MEMBER m_small CONSTANT FINAL)
    Q_PROPERTY(qreal medium MEMBER m_medium CONSTANT FINAL)
    Q_PROPERTY(qreal large MEMBER m_large CONSTANT FINAL)
    Q_PROPERTY(qreal extraLarge MEMBER m_extraLarge CONSTANT FINAL)
    // Fully rounded (pill): Qt clamps a radius to half of the item's smaller side.
    Q_PROPERTY(qreal full MEMBER m_full CONSTANT FINAL)

public:
    using QObject::QObject;

private:
    qreal m_none = 0;
    qreal m_extraSmall = 4;
    qreal m_small = 8;
    qreal m_medium = 12;
    qreal m_large = 16;
    qreal m_extraLarge = 28;
    qreal m_full = 10000;
};

// State layer opacities and disabled-state opacities.
class ThemeState : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(qreal hover MEMBER m_hover CONSTANT FINAL)
    Q_PROPERTY(qreal focus MEMBER m_focus CONSTANT FINAL)
    Q_PROPERTY(qreal pressed MEMBER m_pressed CONSTANT FINAL)
    Q_PROPERTY(qreal dragged MEMBER m_dragged CONSTANT FINAL)
    Q_PROPERTY(qreal disabledContent MEMBER m_disabledContent CONSTANT FINAL)
    Q_PROPERTY(qreal disabledContainer MEMBER m_disabledContainer CONSTANT FINAL)

public:
    using QObject::QObject;

private:
    qreal m_hover = 0.08;
    qreal m_focus = 0.10;
    qreal m_pressed = 0.10;
    qreal m_dragged = 0.16;
    qreal m_disabledContent = 0.38;
    qreal m_disabledContainer = 0.12;
};

// Spacing on the 4 dp grid, and component sizes adjusted by the density setting.
class ThemeSpace : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(qreal xxs MEMBER m_xxs CONSTANT FINAL)
    Q_PROPERTY(qreal xs MEMBER m_xs CONSTANT FINAL)
    Q_PROPERTY(qreal sm MEMBER m_sm CONSTANT FINAL)
    Q_PROPERTY(qreal md MEMBER m_md CONSTANT FINAL)
    Q_PROPERTY(qreal lg MEMBER m_lg CONSTANT FINAL)
    Q_PROPERTY(qreal xl MEMBER m_xl CONSTANT FINAL)
    Q_PROPERTY(qreal xxl MEMBER m_xxl CONSTANT FINAL)
    Q_PROPERTY(qreal xxxl MEMBER m_xxxl CONSTANT FINAL)
    // Density: 0 = default, -1/-2 = compact (each step removes 4 dp from control heights, M3 density).
    Q_PROPERTY(int density READ density NOTIFY densityChanged FINAL)
    // Minimum touch/click target (M3: 48 dp; 40 dp in compact density on desktop).
    Q_PROPERTY(qreal minimumTarget READ minimumTarget NOTIFY densityChanged FINAL)

public:
    using QObject::QObject;

    int density() const { return m_density; }
    void setDensity(int density)
    {
        if (density != m_density) {
            m_density = density;
            emit densityChanged();
        }
    }
    qreal minimumTarget() const { return m_density < 0 ? 40 : 48; }
    // Height of a control whose default M3 height is `base`, adjusted for the density.
    Q_INVOKABLE qreal control(qreal base) const { return base + 4 * m_density; }

signals:
    void densityChanged();

private:
    qreal m_xxs = 2;
    qreal m_xs = 4;
    qreal m_sm = 8;
    qreal m_md = 12;
    qreal m_lg = 16;
    qreal m_xl = 24;
    qreal m_xxl = 32;
    qreal m_xxxl = 48;
    int m_density = 0;
};

// Sizes of the editor's own surfaces (timeline, panels, preview), so that QML never hard-codes a dimension.
class ThemeEditor : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // Panels
    Q_PROPERTY(qreal libraryWidth MEMBER m_libraryWidth CONSTANT FINAL)
    Q_PROPERTY(qreal mediaTileWidth MEMBER m_mediaTileWidth CONSTANT FINAL)
    Q_PROPERTY(qreal mediaTileHeight MEMBER m_mediaTileHeight CONSTANT FINAL)
    Q_PROPERTY(qreal previewMinimumHeight MEMBER m_previewMinimumHeight CONSTANT FINAL)
    Q_PROPERTY(qreal timelineMinimumHeight MEMBER m_timelineMinimumHeight CONSTANT FINAL)
    Q_PROPERTY(qreal timelineDefaultHeight MEMBER m_timelineDefaultHeight CONSTANT FINAL)
    Q_PROPERTY(qreal splitterSize MEMBER m_splitterSize CONSTANT FINAL)
    Q_PROPERTY(qreal toolbarHeight MEMBER m_toolbarHeight CONSTANT FINAL)
    Q_PROPERTY(qreal dialogWidth MEMBER m_dialogWidth CONSTANT FINAL)
    // Editor chrome: top bar, library rail, the compact icon buttons of the toolbars and panel headers
    Q_PROPERTY(qreal topBarHeight MEMBER m_topBarHeight CONSTANT FINAL)
    Q_PROPERTY(qreal panelHeaderHeight MEMBER m_panelHeaderHeight CONSTANT FINAL)
    Q_PROPERTY(qreal railWidth MEMBER m_railWidth CONSTANT FINAL)
    Q_PROPERTY(qreal railItemHeight MEMBER m_railItemHeight CONSTANT FINAL)
    Q_PROPERTY(qreal toolButtonSize MEMBER m_toolButtonSize CONSTANT FINAL)
    Q_PROPERTY(qreal toolIconSize MEMBER m_toolIconSize CONSTANT FINAL)
    Q_PROPERTY(qreal smallIconSize MEMBER m_smallIconSize CONSTANT FINAL)
    Q_PROPERTY(qreal badgeHeight MEMBER m_badgeHeight CONSTANT FINAL)
    Q_PROPERTY(qreal panelMinimumWidth MEMBER m_panelMinimumWidth CONSTANT FINAL)
    Q_PROPERTY(qreal playButtonSize MEMBER m_playButtonSize CONSTANT FINAL)
    // Properties panel (right) and asset libraries (left)
    Q_PROPERTY(qreal propertiesWidth MEMBER m_propertiesWidth CONSTANT FINAL)
    Q_PROPERTY(qreal tabIndicator MEMBER m_tabIndicator CONSTANT FINAL)
    Q_PROPERTY(qreal swatchSize MEMBER m_swatchSize CONSTANT FINAL)
    Q_PROPERTY(qreal valueWidth MEMBER m_valueWidth CONSTANT FINAL)
    // A property as one row (name · slider · value box) when the panel is at least this wide; the name's width there,
    // and the height of the value box
    Q_PROPERTY(qreal inlinePropertyWidth MEMBER m_inlinePropertyWidth CONSTANT FINAL)
    Q_PROPERTY(qreal propertyLabelWidth MEMBER m_propertyLabelWidth CONSTANT FINAL)
    Q_PROPERTY(qreal valueFieldHeight MEMBER m_valueFieldHeight CONSTANT FINAL)
    Q_PROPERTY(qreal textAreaHeight MEMBER m_textAreaHeight CONSTANT FINAL)
    Q_PROPERTY(qreal assetTileWidth MEMBER m_assetTileWidth CONSTANT FINAL)
    Q_PROPERTY(qreal assetTileHeight MEMBER m_assetTileHeight CONSTANT FINAL)
    // Canvas handles on the preview and transition marks on the timeline
    Q_PROPERTY(qreal handleSize MEMBER m_handleSize CONSTANT FINAL)
    Q_PROPERTY(qreal rotateHandleDistance MEMBER m_rotateHandleDistance CONSTANT FINAL)
    Q_PROPERTY(qreal transitionMark MEMBER m_transitionMark CONSTANT FINAL)
    Q_PROPERTY(qreal meterWidth MEMBER m_meterWidth CONSTANT FINAL)
    // Refresh of the audio meters (ms): not an animation, so not affected by "reduce motion".
    Q_PROPERTY(qreal meterInterval MEMBER m_meterInterval CONSTANT FINAL)
    // Delays that are not animations (not affected by "reduce motion"): controls that hide by themselves (ms), the
    // step of the looping previews of the libraries (ms)
    Q_PROPERTY(qreal autoHideDelay MEMBER m_autoHideDelay CONSTANT FINAL)
    Q_PROPERTY(qreal previewLoopInterval MEMBER m_previewLoopInterval CONSTANT FINAL)
    // Home screen
    Q_PROPERTY(qreal draftCardWidth MEMBER m_draftCardWidth CONSTANT FINAL)
    Q_PROPERTY(qreal draftThumbnailHeight MEMBER m_draftThumbnailHeight CONSTANT FINAL)
    Q_PROPERTY(qreal heroHeight MEMBER m_heroHeight CONSTANT FINAL)
    Q_PROPERTY(qreal contentMaxWidth MEMBER m_contentMaxWidth CONSTANT FINAL)
    // Timeline
    Q_PROPERTY(qreal rulerHeight MEMBER m_rulerHeight CONSTANT FINAL)
    Q_PROPERTY(qreal trackHeaderWidth MEMBER m_trackHeaderWidth CONSTANT FINAL)
    // Cover tile at the head of the main track (SPEC §4), the label strip of a clip, ruler ticks
    Q_PROPERTY(qreal coverWidth MEMBER m_coverWidth CONSTANT FINAL)
    Q_PROPERTY(qreal clipLabelHeight MEMBER m_clipLabelHeight CONSTANT FINAL)
    Q_PROPERTY(qreal rulerMajorTick MEMBER m_rulerMajorTick CONSTANT FINAL)
    Q_PROPERTY(qreal rulerMinorTick MEMBER m_rulerMinorTick CONSTANT FINAL)
    Q_PROPERTY(qreal gripSize MEMBER m_gripSize CONSTANT FINAL)
    Q_PROPERTY(qreal mainTrackHeight MEMBER m_mainTrackHeight CONSTANT FINAL)
    Q_PROPERTY(qreal overlayTrackHeight MEMBER m_overlayTrackHeight CONSTANT FINAL)
    Q_PROPERTY(qreal audioTrackHeight MEMBER m_audioTrackHeight CONSTANT FINAL)
    Q_PROPERTY(qreal trackGap MEMBER m_trackGap CONSTANT FINAL)
    Q_PROPERTY(qreal newTrackZone MEMBER m_newTrackZone CONSTANT FINAL)
    Q_PROPERTY(qreal trimHandleWidth MEMBER m_trimHandleWidth CONSTANT FINAL)
    Q_PROPERTY(qreal playheadWidth MEMBER m_playheadWidth CONSTANT FINAL)
    Q_PROPERTY(qreal playheadKnob MEMBER m_playheadKnob CONSTANT FINAL)
    Q_PROPERTY(qreal selectionBorder MEMBER m_selectionBorder CONSTANT FINAL)
    Q_PROPERTY(qreal hairline MEMBER m_hairline CONSTANT FINAL)
    Q_PROPERTY(qreal snapThreshold MEMBER m_snapThreshold CONSTANT FINAL)
    Q_PROPERTY(qreal rulerLabelSpacing MEMBER m_rulerLabelSpacing CONSTANT FINAL)
    Q_PROPERTY(qreal dragStartDistance MEMBER m_dragStartDistance CONSTANT FINAL)
    // Zoom of the timeline in pixels per frame
    Q_PROPERTY(qreal zoomDefault MEMBER m_zoomDefault CONSTANT FINAL)
    Q_PROPERTY(qreal zoomMinimum MEMBER m_zoomMinimum CONSTANT FINAL)
    Q_PROPERTY(qreal zoomMaximum MEMBER m_zoomMaximum CONSTANT FINAL)
    Q_PROPERTY(qreal zoomStep MEMBER m_zoomStep CONSTANT FINAL)
    // The pictures of a clip (frames, waveform) are painted only around the view, in steps of this width
    Q_PROPERTY(qreal paintChunk MEMBER m_paintChunk CONSTANT FINAL)
    // Timeline padding after the last clip, as a share of the view
    Q_PROPERTY(qreal tailRatio MEMBER m_tailRatio CONSTANT FINAL)

public:
    using QObject::QObject;

private:
    qreal m_libraryWidth = 344;
    qreal m_mediaTileWidth = 152;
    qreal m_mediaTileHeight = 100;
    qreal m_previewMinimumHeight = 180;
    qreal m_timelineMinimumHeight = 160;
    qreal m_timelineDefaultHeight = 300;
    qreal m_splitterSize = 8;
    qreal m_toolbarHeight = 48;
    qreal m_dialogWidth = 560;
    qreal m_topBarHeight = 48;
    qreal m_panelHeaderHeight = 36;
    qreal m_railWidth = 80;
    qreal m_railItemHeight = 52;
    qreal m_toolButtonSize = 32;
    qreal m_toolIconSize = 20;
    qreal m_smallIconSize = 16;
    qreal m_badgeHeight = 20;
    qreal m_panelMinimumWidth = 220;
    qreal m_playButtonSize = 40;
    qreal m_propertiesWidth = 352;
    qreal m_tabIndicator = 3;
    qreal m_swatchSize = 24;
    qreal m_valueWidth = 56;
    qreal m_inlinePropertyWidth = 272;
    qreal m_propertyLabelWidth = 96;
    qreal m_valueFieldHeight = 28;
    qreal m_textAreaHeight = 88;
    qreal m_assetTileWidth = 96;
    qreal m_assetTileHeight = 72;
    qreal m_handleSize = 12;
    qreal m_rotateHandleDistance = 28;
    qreal m_transitionMark = 22;
    qreal m_meterWidth = 6;
    qreal m_meterInterval = 33;
    qreal m_autoHideDelay = 2500;
    qreal m_previewLoopInterval = 75;
    qreal m_draftCardWidth = 232;
    qreal m_draftThumbnailHeight = 130;
    qreal m_heroHeight = 176;
    qreal m_contentMaxWidth = 1240;
    qreal m_rulerHeight = 28;
    qreal m_trackHeaderWidth = 156;
    qreal m_coverWidth = 56;
    qreal m_clipLabelHeight = 18;
    qreal m_rulerMajorTick = 10;
    qreal m_rulerMinorTick = 4;
    qreal m_gripSize = 2;
    qreal m_mainTrackHeight = 64;
    qreal m_overlayTrackHeight = 48;
    qreal m_audioTrackHeight = 40;
    qreal m_trackGap = 4;
    qreal m_newTrackZone = 24;
    qreal m_trimHandleWidth = 10;
    qreal m_playheadWidth = 2;
    qreal m_playheadKnob = 12;
    qreal m_selectionBorder = 2;
    qreal m_hairline = 1;
    qreal m_snapThreshold = 8;
    qreal m_rulerLabelSpacing = 96;
    qreal m_dragStartDistance = 4;
    qreal m_zoomDefault = 3;
    qreal m_zoomMinimum = 0.02;
    qreal m_zoomMaximum = 24;
    qreal m_zoomStep = 1.25;
    qreal m_paintChunk = 512;
    qreal m_tailRatio = 0.5;
};

// Elevation levels 0–5 (dp), rendered mainly with tonal surface colors (see Theme.surfaceAt()).
class ThemeElevation : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(qreal level0 MEMBER m_level0 CONSTANT FINAL)
    Q_PROPERTY(qreal level1 MEMBER m_level1 CONSTANT FINAL)
    Q_PROPERTY(qreal level2 MEMBER m_level2 CONSTANT FINAL)
    Q_PROPERTY(qreal level3 MEMBER m_level3 CONSTANT FINAL)
    Q_PROPERTY(qreal level4 MEMBER m_level4 CONSTANT FINAL)
    Q_PROPERTY(qreal level5 MEMBER m_level5 CONSTANT FINAL)

public:
    using QObject::QObject;

private:
    qreal m_level0 = 0;
    qreal m_level1 = 1;
    qreal m_level2 = 3;
    qreal m_level3 = 6;
    qreal m_level4 = 8;
    qreal m_level5 = 12;
};

// Motion: M3 durations (ms) and easing curves (for easing.type: Easing.BezierSpline). With "reduce
// motion" every non-essential duration becomes 0; `essential*` durations are kept for motion that
// carries information (e.g. progress indicators).
class ThemeMotion : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool reduced READ reduced NOTIFY changed FINAL)
    Q_PROPERTY(int short1 READ short1 NOTIFY changed FINAL)
    Q_PROPERTY(int short2 READ short2 NOTIFY changed FINAL)
    Q_PROPERTY(int short3 READ short3 NOTIFY changed FINAL)
    Q_PROPERTY(int short4 READ short4 NOTIFY changed FINAL)
    Q_PROPERTY(int medium1 READ medium1 NOTIFY changed FINAL)
    Q_PROPERTY(int medium2 READ medium2 NOTIFY changed FINAL)
    Q_PROPERTY(int medium3 READ medium3 NOTIFY changed FINAL)
    Q_PROPERTY(int medium4 READ medium4 NOTIFY changed FINAL)
    Q_PROPERTY(int long1 READ long1 NOTIFY changed FINAL)
    Q_PROPERTY(int long2 READ long2 NOTIFY changed FINAL)
    Q_PROPERTY(int long3 READ long3 NOTIFY changed FINAL)
    Q_PROPERTY(int long4 READ long4 NOTIFY changed FINAL)
    Q_PROPERTY(int essentialMedium READ essentialMedium CONSTANT FINAL)
    Q_PROPERTY(int essentialLong READ essentialLong CONSTANT FINAL)
    Q_PROPERTY(QVariantList emphasized READ emphasized CONSTANT FINAL)
    Q_PROPERTY(QVariantList emphasizedDecelerate READ emphasizedDecelerate CONSTANT FINAL)
    Q_PROPERTY(QVariantList emphasizedAccelerate READ emphasizedAccelerate CONSTANT FINAL)
    Q_PROPERTY(QVariantList standard READ standard CONSTANT FINAL)
    Q_PROPERTY(QVariantList standardDecelerate READ standardDecelerate CONSTANT FINAL)
    Q_PROPERTY(QVariantList standardAccelerate READ standardAccelerate CONSTANT FINAL)

public:
    using QObject::QObject;

    bool reduced() const { return m_reduced; }
    void setReduced(bool reduced)
    {
        if (reduced != m_reduced) {
            m_reduced = reduced;
            emit changed();
        }
    }

    int short1() const { return value(50); }
    int short2() const { return value(100); }
    int short3() const { return value(150); }
    int short4() const { return value(200); }
    int medium1() const { return value(250); }
    int medium2() const { return value(300); }
    int medium3() const { return value(350); }
    int medium4() const { return value(400); }
    int long1() const { return value(450); }
    int long2() const { return value(500); }
    int long3() const { return value(550); }
    int long4() const { return value(600); }
    int essentialMedium() const { return 300; }
    int essentialLong() const { return 500; }

    // QML BezierSpline format: [c1x, c1y, c2x, c2y, endX, endY, ...].
    static QVariantList emphasized()
    {
        return {0.05, 0.0, 0.133333, 0.06, 0.166666, 0.4, 0.208333, 0.82, 0.25, 1.0, 1.0, 1.0};
    }
    static QVariantList emphasizedDecelerate() { return {0.05, 0.7, 0.1, 1.0, 1.0, 1.0}; }
    static QVariantList emphasizedAccelerate() { return {0.3, 0.0, 0.8, 0.15, 1.0, 1.0}; }
    static QVariantList standard() { return {0.2, 0.0, 0.0, 1.0, 1.0, 1.0}; }
    static QVariantList standardDecelerate() { return {0.0, 0.0, 0.0, 1.0, 1.0, 1.0}; }
    static QVariantList standardAccelerate() { return {0.3, 0.0, 1.0, 1.0, 1.0, 1.0}; }

signals:
    void changed();

private:
    int value(int milliseconds) const { return m_reduced ? 0 : milliseconds; }

    bool m_reduced = false;
};

} // namespace vedit::theme
