// SPDX-License-Identifier: GPL-3.0-or-later
// Generated role list (see ColorScheme.h): one QML property per Material 3 color role.
#pragma once

#include "theme/ColorScheme.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace vedit::theme {

class ThemeColors : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QColor primary READ primary NOTIFY changed FINAL)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY changed FINAL)
    Q_PROPERTY(QColor primaryContainer READ primaryContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor onPrimaryContainer READ onPrimaryContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor inversePrimary READ inversePrimary NOTIFY changed FINAL)
    Q_PROPERTY(QColor secondary READ secondary NOTIFY changed FINAL)
    Q_PROPERTY(QColor onSecondary READ onSecondary NOTIFY changed FINAL)
    Q_PROPERTY(QColor secondaryContainer READ secondaryContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor onSecondaryContainer READ onSecondaryContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor tertiary READ tertiary NOTIFY changed FINAL)
    Q_PROPERTY(QColor onTertiary READ onTertiary NOTIFY changed FINAL)
    Q_PROPERTY(QColor tertiaryContainer READ tertiaryContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor onTertiaryContainer READ onTertiaryContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor error READ error NOTIFY changed FINAL)
    Q_PROPERTY(QColor onError READ onError NOTIFY changed FINAL)
    Q_PROPERTY(QColor errorContainer READ errorContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor onErrorContainer READ onErrorContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor background READ background NOTIFY changed FINAL)
    Q_PROPERTY(QColor onBackground READ onBackground NOTIFY changed FINAL)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed FINAL)
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceVariant READ surfaceVariant NOTIFY changed FINAL)
    Q_PROPERTY(QColor onSurfaceVariant READ onSurfaceVariant NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceDim READ surfaceDim NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceBright READ surfaceBright NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceContainerLowest READ surfaceContainerLowest NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceContainerLow READ surfaceContainerLow NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceContainer READ surfaceContainer NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceContainerHigh READ surfaceContainerHigh NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceContainerHighest READ surfaceContainerHighest NOTIFY changed FINAL)
    Q_PROPERTY(QColor inverseSurface READ inverseSurface NOTIFY changed FINAL)
    Q_PROPERTY(QColor inverseOnSurface READ inverseOnSurface NOTIFY changed FINAL)
    Q_PROPERTY(QColor outline READ outline NOTIFY changed FINAL)
    Q_PROPERTY(QColor outlineVariant READ outlineVariant NOTIFY changed FINAL)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY changed FINAL)
    Q_PROPERTY(QColor scrim READ scrim NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceTint READ surfaceTint NOTIFY changed FINAL)
    Q_PROPERTY(QColor primaryFixed READ primaryFixed NOTIFY changed FINAL)
    Q_PROPERTY(QColor primaryFixedDim READ primaryFixedDim NOTIFY changed FINAL)
    Q_PROPERTY(QColor onPrimaryFixed READ onPrimaryFixed NOTIFY changed FINAL)
    Q_PROPERTY(QColor onPrimaryFixedVariant READ onPrimaryFixedVariant NOTIFY changed FINAL)
    Q_PROPERTY(QColor secondaryFixed READ secondaryFixed NOTIFY changed FINAL)
    Q_PROPERTY(QColor secondaryFixedDim READ secondaryFixedDim NOTIFY changed FINAL)
    Q_PROPERTY(QColor onSecondaryFixed READ onSecondaryFixed NOTIFY changed FINAL)
    Q_PROPERTY(QColor onSecondaryFixedVariant READ onSecondaryFixedVariant NOTIFY changed FINAL)
    Q_PROPERTY(QColor tertiaryFixed READ tertiaryFixed NOTIFY changed FINAL)
    Q_PROPERTY(QColor tertiaryFixedDim READ tertiaryFixedDim NOTIFY changed FINAL)
    Q_PROPERTY(QColor onTertiaryFixed READ onTertiaryFixed NOTIFY changed FINAL)
    Q_PROPERTY(QColor onTertiaryFixedVariant READ onTertiaryFixedVariant NOTIFY changed FINAL)

public:
    using QObject::QObject;

    void setScheme(const ColorScheme &scheme)
    {
        if (scheme == m_scheme) {
            return;
        }
        m_scheme = scheme;
        emit changed();
    }
    const ColorScheme &scheme() const noexcept { return m_scheme; }

    QColor primary() const { return m_scheme[ColorRole::Primary]; }
    QColor onPrimary() const { return m_scheme[ColorRole::OnPrimary]; }
    QColor primaryContainer() const { return m_scheme[ColorRole::PrimaryContainer]; }
    QColor onPrimaryContainer() const { return m_scheme[ColorRole::OnPrimaryContainer]; }
    QColor inversePrimary() const { return m_scheme[ColorRole::InversePrimary]; }
    QColor secondary() const { return m_scheme[ColorRole::Secondary]; }
    QColor onSecondary() const { return m_scheme[ColorRole::OnSecondary]; }
    QColor secondaryContainer() const { return m_scheme[ColorRole::SecondaryContainer]; }
    QColor onSecondaryContainer() const { return m_scheme[ColorRole::OnSecondaryContainer]; }
    QColor tertiary() const { return m_scheme[ColorRole::Tertiary]; }
    QColor onTertiary() const { return m_scheme[ColorRole::OnTertiary]; }
    QColor tertiaryContainer() const { return m_scheme[ColorRole::TertiaryContainer]; }
    QColor onTertiaryContainer() const { return m_scheme[ColorRole::OnTertiaryContainer]; }
    QColor error() const { return m_scheme[ColorRole::Error]; }
    QColor onError() const { return m_scheme[ColorRole::OnError]; }
    QColor errorContainer() const { return m_scheme[ColorRole::ErrorContainer]; }
    QColor onErrorContainer() const { return m_scheme[ColorRole::OnErrorContainer]; }
    QColor background() const { return m_scheme[ColorRole::Background]; }
    QColor onBackground() const { return m_scheme[ColorRole::OnBackground]; }
    QColor surface() const { return m_scheme[ColorRole::Surface]; }
    QColor onSurface() const { return m_scheme[ColorRole::OnSurface]; }
    QColor surfaceVariant() const { return m_scheme[ColorRole::SurfaceVariant]; }
    QColor onSurfaceVariant() const { return m_scheme[ColorRole::OnSurfaceVariant]; }
    QColor surfaceDim() const { return m_scheme[ColorRole::SurfaceDim]; }
    QColor surfaceBright() const { return m_scheme[ColorRole::SurfaceBright]; }
    QColor surfaceContainerLowest() const { return m_scheme[ColorRole::SurfaceContainerLowest]; }
    QColor surfaceContainerLow() const { return m_scheme[ColorRole::SurfaceContainerLow]; }
    QColor surfaceContainer() const { return m_scheme[ColorRole::SurfaceContainer]; }
    QColor surfaceContainerHigh() const { return m_scheme[ColorRole::SurfaceContainerHigh]; }
    QColor surfaceContainerHighest() const { return m_scheme[ColorRole::SurfaceContainerHighest]; }
    QColor inverseSurface() const { return m_scheme[ColorRole::InverseSurface]; }
    QColor inverseOnSurface() const { return m_scheme[ColorRole::InverseOnSurface]; }
    QColor outline() const { return m_scheme[ColorRole::Outline]; }
    QColor outlineVariant() const { return m_scheme[ColorRole::OutlineVariant]; }
    QColor shadow() const { return m_scheme[ColorRole::Shadow]; }
    QColor scrim() const { return m_scheme[ColorRole::Scrim]; }
    QColor surfaceTint() const { return m_scheme[ColorRole::SurfaceTint]; }
    QColor primaryFixed() const { return m_scheme[ColorRole::PrimaryFixed]; }
    QColor primaryFixedDim() const { return m_scheme[ColorRole::PrimaryFixedDim]; }
    QColor onPrimaryFixed() const { return m_scheme[ColorRole::OnPrimaryFixed]; }
    QColor onPrimaryFixedVariant() const { return m_scheme[ColorRole::OnPrimaryFixedVariant]; }
    QColor secondaryFixed() const { return m_scheme[ColorRole::SecondaryFixed]; }
    QColor secondaryFixedDim() const { return m_scheme[ColorRole::SecondaryFixedDim]; }
    QColor onSecondaryFixed() const { return m_scheme[ColorRole::OnSecondaryFixed]; }
    QColor onSecondaryFixedVariant() const { return m_scheme[ColorRole::OnSecondaryFixedVariant]; }
    QColor tertiaryFixed() const { return m_scheme[ColorRole::TertiaryFixed]; }
    QColor tertiaryFixedDim() const { return m_scheme[ColorRole::TertiaryFixedDim]; }
    QColor onTertiaryFixed() const { return m_scheme[ColorRole::OnTertiaryFixed]; }
    QColor onTertiaryFixedVariant() const { return m_scheme[ColorRole::OnTertiaryFixedVariant]; }

signals:
    void changed();

private:
    ColorScheme m_scheme;
};

} // namespace vedit::theme
