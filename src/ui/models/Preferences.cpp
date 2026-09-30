// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"

#include <QLocale>

using namespace Qt::StringLiterals;

namespace vedit::ui {

Preferences::Preferences(QObject *parent)
    : QObject(parent)
    , m_settings(u"vedit"_s, u"vedit"_s)
{
}

QString Preferences::language() const
{
    return m_settings.value(u"language"_s, u"auto"_s).toString();
}

void Preferences::setLanguage(const QString &language)
{
    if (language != this->language()) {
        m_settings.setValue(u"language"_s, language);
        emit languageChanged();
    }
}

int Preferences::autoSaveInterval() const
{
    return m_settings.value(u"autoSaveInterval"_s, 2).toInt();
}

void Preferences::setAutoSaveInterval(int seconds)
{
    if (seconds != autoSaveInterval() && seconds >= 1 && seconds <= 60) {
        m_settings.setValue(u"autoSaveInterval"_s, seconds);
        emit autoSaveIntervalChanged();
    }
}

bool Preferences::hardwareAcceleration() const
{
    return m_settings.value(u"hardwareAcceleration"_s, true).toBool();
}

void Preferences::setHardwareAcceleration(bool enabled)
{
    if (enabled != hardwareAcceleration()) {
        m_settings.setValue(u"hardwareAcceleration"_s, enabled);
        emit hardwareAccelerationChanged();
    }
}

int Preferences::proxyScale() const
{
    return m_settings.value(u"proxyScale"_s, 0).toInt();
}

void Preferences::setProxyScale(int scale)
{
    if (scale != proxyScale() && scale >= 0 && scale <= 3) {
        m_settings.setValue(u"proxyScale"_s, scale);
        emit proxyScaleChanged();
    }
}

void Preferences::resetToDefaults()
{
    m_settings.clear();
    emit languageChanged();
    emit autoSaveIntervalChanged();
    emit hardwareAccelerationChanged();
    emit proxyScaleChanged();
}

} // namespace vedit::ui
