// SPDX-License-Identifier: GPL-3.0-or-later
// Material 3 type scale (docs/DESIGN_SYSTEM.md). Generated from the table in ThemeTypography.cpp.
#pragma once

#include <QFont>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace velacut::theme {

class ThemeTypography : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QFont displayLarge READ displayLarge NOTIFY changed FINAL)
    Q_PROPERTY(qreal displayLargeLineHeight READ displayLargeLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont displayMedium READ displayMedium NOTIFY changed FINAL)
    Q_PROPERTY(qreal displayMediumLineHeight READ displayMediumLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont displaySmall READ displaySmall NOTIFY changed FINAL)
    Q_PROPERTY(qreal displaySmallLineHeight READ displaySmallLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont headlineLarge READ headlineLarge NOTIFY changed FINAL)
    Q_PROPERTY(qreal headlineLargeLineHeight READ headlineLargeLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont headlineMedium READ headlineMedium NOTIFY changed FINAL)
    Q_PROPERTY(qreal headlineMediumLineHeight READ headlineMediumLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont headlineSmall READ headlineSmall NOTIFY changed FINAL)
    Q_PROPERTY(qreal headlineSmallLineHeight READ headlineSmallLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont titleLarge READ titleLarge NOTIFY changed FINAL)
    Q_PROPERTY(qreal titleLargeLineHeight READ titleLargeLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont titleMedium READ titleMedium NOTIFY changed FINAL)
    Q_PROPERTY(qreal titleMediumLineHeight READ titleMediumLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont titleSmall READ titleSmall NOTIFY changed FINAL)
    Q_PROPERTY(qreal titleSmallLineHeight READ titleSmallLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont bodyLarge READ bodyLarge NOTIFY changed FINAL)
    Q_PROPERTY(qreal bodyLargeLineHeight READ bodyLargeLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont bodyMedium READ bodyMedium NOTIFY changed FINAL)
    Q_PROPERTY(qreal bodyMediumLineHeight READ bodyMediumLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont bodySmall READ bodySmall NOTIFY changed FINAL)
    Q_PROPERTY(qreal bodySmallLineHeight READ bodySmallLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont labelLarge READ labelLarge NOTIFY changed FINAL)
    Q_PROPERTY(qreal labelLargeLineHeight READ labelLargeLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont labelMedium READ labelMedium NOTIFY changed FINAL)
    Q_PROPERTY(qreal labelMediumLineHeight READ labelMediumLineHeight NOTIFY changed FINAL)
    Q_PROPERTY(QFont labelSmall READ labelSmall NOTIFY changed FINAL)
    Q_PROPERTY(qreal labelSmallLineHeight READ labelSmallLineHeight NOTIFY changed FINAL)

public:
    using QObject::QObject;

    // Font family used for every style; sizes are logical pixels (device independent) times `scale`.
    void configure(const QString &family, qreal scale);

    QFont displayLarge() const { return font(0); }
    qreal displayLargeLineHeight() const { return lineHeight(0); }
    QFont displayMedium() const { return font(1); }
    qreal displayMediumLineHeight() const { return lineHeight(1); }
    QFont displaySmall() const { return font(2); }
    qreal displaySmallLineHeight() const { return lineHeight(2); }
    QFont headlineLarge() const { return font(3); }
    qreal headlineLargeLineHeight() const { return lineHeight(3); }
    QFont headlineMedium() const { return font(4); }
    qreal headlineMediumLineHeight() const { return lineHeight(4); }
    QFont headlineSmall() const { return font(5); }
    qreal headlineSmallLineHeight() const { return lineHeight(5); }
    QFont titleLarge() const { return font(6); }
    qreal titleLargeLineHeight() const { return lineHeight(6); }
    QFont titleMedium() const { return font(7); }
    qreal titleMediumLineHeight() const { return lineHeight(7); }
    QFont titleSmall() const { return font(8); }
    qreal titleSmallLineHeight() const { return lineHeight(8); }
    QFont bodyLarge() const { return font(9); }
    qreal bodyLargeLineHeight() const { return lineHeight(9); }
    QFont bodyMedium() const { return font(10); }
    qreal bodyMediumLineHeight() const { return lineHeight(10); }
    QFont bodySmall() const { return font(11); }
    qreal bodySmallLineHeight() const { return lineHeight(11); }
    QFont labelLarge() const { return font(12); }
    qreal labelLargeLineHeight() const { return lineHeight(12); }
    QFont labelMedium() const { return font(13); }
    qreal labelMediumLineHeight() const { return lineHeight(13); }
    QFont labelSmall() const { return font(14); }
    qreal labelSmallLineHeight() const { return lineHeight(14); }

    static constexpr int kStyleCount = 15;

signals:
    void changed();

private:
    QFont font(int style) const;
    qreal lineHeight(int style) const;

    QString m_family;
    qreal m_scale = 1.0;
};

} // namespace velacut::theme
