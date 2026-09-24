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
