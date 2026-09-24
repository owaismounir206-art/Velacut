// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemAppearance.h"

#include "common/DevSandbox.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace vedit::theme {

namespace {

const QString kPortalService = u"org.freedesktop.portal.Desktop"_s;
const QString kPortalPath = u"/org/freedesktop/portal/desktop"_s;
const QString kSettingsInterface = u"org.freedesktop.portal.Settings"_s;
const QString kAppearance = u"org.freedesktop.appearance"_s;
constexpr int kPortalTimeoutMs = 300;
constexpr int kProcessTimeoutMs = 700;

// Environment of the user's session (the development sandbox redirects XDG directories, but desktop
// tools such as gsettings must read the real configuration).
QProcessEnvironment hostEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const char *variable : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"}) {
        const QString host = u"VEDIT_HOST_"_s + QLatin1StringView(variable);
        if (environment.contains(host)) {
            const QString value = environment.value(host);
            if (value.isEmpty()) {
                environment.remove(QLatin1StringView(variable));
            } else {
                environment.insert(QLatin1StringView(variable), value);
            }
        }
    }
    return environment;
}

std::optional<QString> runTool(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProcessEnvironment(hostEnvironment());
    process.start(program, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(kProcessTimeoutMs)) {
        return std::nullopt; // tool not installed
    }
    if (!process.waitForFinished(kProcessTimeoutMs) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        process.kill();
        process.waitForFinished(100);
        return std::nullopt;
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QVariant unwrap(const QVariant &value)
{
    // ReadOne returns v; Read (older portals) returns v wrapped in another v.
    QVariant current = value;
    while (current.canConvert<QDBusVariant>() && current.userType() == qMetaTypeId<QDBusVariant>()) {
        current = current.value<QDBusVariant>().variant();
    }
    return current;
}

std::optional<QVariant> readPortal(const QString &key)
{
    for (const QString &method : {u"ReadOne"_s, u"Read"_s}) {
        QDBusMessage call = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kSettingsInterface, method);
        call << kAppearance << key;
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, kPortalTimeoutMs);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            return unwrap(reply.arguments().constFirst());
        }
        // NotFound means "this portal does not provide the key": no need to try the old method.
        if (reply.errorName() == u"org.freedesktop.portal.Error.NotFound"_s) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<QString> localFile(const QString &uriOrPath)
{
    QString path = uriOrPath.trimmed();
    if (path.startsWith(u"file://"_s)) {
        path = QUrl(path).toLocalFile();
    }
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        return std::nullopt;
    }
    return path;
}

QString stripQuotes(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && (text.front() == u'\'' || text.front() == u'"') && text.back() == text.front()) {
        text = text.mid(1, text.size() - 2);
    }
    return text;
}

} // namespace

SystemAppearance::SystemAppearance(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(kPortalService, kPortalPath, kSettingsInterface, u"SettingChanged"_s, this,
                                          SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));
}

void SystemAppearance::refresh()
{
    for (const QString &key : {u"color-scheme"_s, u"contrast"_s, u"reduced-motion"_s, u"accent-color"_s}) {
        if (const auto value = readPortal(key)) {
            applyPortalValue(key, *value);
        }
    }
    if (!m_portalAccent) {
        readFallbackAccent();
    }
    emit changed();
}

void SystemAppearance::applyPortalValue(const QString &key, const QVariant &value)
{
    if (key == u"color-scheme"_s) {
        switch (value.toUInt()) {
        case 1:
            m_colorScheme = ColorSchemePreference::Dark;
            break;
        case 2:
            m_colorScheme = ColorSchemePreference::Light;
            break;
        default:
            m_colorScheme = ColorSchemePreference::NoPreference;
            break;
        }
    } else if (key == u"contrast"_s) {
        m_highContrast = value.toUInt() == 1;
    } else if (key == u"reduced-motion"_s) {
        m_reducedMotion = value.toUInt() == 1;
    } else if (key == u"accent-color"_s) {
        // Type (ddd): sRGB components in [0, 1]; values outside the range mean "no accent".
        if (value.canConvert<QDBusArgument>()) {
            const QDBusArgument argument = value.value<QDBusArgument>();
            double red = -1.0;
            double green = -1.0;
            double blue = -1.0;
            argument.beginStructure();
            argument >> red >> green >> blue;
            argument.endStructure();
            m_accentColor = parsePortalAccent(red, green, blue);
            m_portalAccent = m_accentColor.has_value();
            m_accentSource = m_portalAccent ? u"portal"_s : QString();
        }
    }
}

void SystemAppearance::onPortalSettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value)
{
    if (nameSpace != kAppearance) {
        return;
    }
    applyPortalValue(key, unwrap(value.variant()));
    if (key == u"accent-color"_s && !m_portalAccent) {
        readFallbackAccent();
    }
    emit changed();
}

void SystemAppearance::readFallbackAccent()
{
    // GNOME (and GTK-based sessions): named accent in gsettings.
    if (const auto output = runTool(u"gsettings"_s, {u"get"_s, u"org.gnome.desktop.interface"_s, u"accent-color"_s})) {
        if (const auto color = parseGnomeAccent(*output)) {
            m_accentColor = color;
            m_accentSource = u"gnome"_s;
            return;
        }
    }
    // KDE Plasma: AccentColor in kdeglobals.
    QFile kdeglobals(hostConfigHome() + u"/kdeglobals"_s);
    if (kdeglobals.open(QIODevice::ReadOnly)) {
        if (const auto color = parseKdeAccent(QString::fromUtf8(kdeglobals.readAll()))) {
            m_accentColor = color;
            m_accentSource = u"kde"_s;
            return;
        }
    }
    m_accentColor.reset();
    m_accentSource.clear();
}

std::optional<QString> SystemAppearance::wallpaperPath()
{
    if (const auto output = runTool(u"gsettings"_s, {u"get"_s, u"org.gnome.desktop.background"_s, u"picture-uri-dark"_s})) {
        if (const auto path = parseGnomeWallpaper(*output)) {
            return path;
        }
    }
    if (const auto output = runTool(u"gsettings"_s, {u"get"_s, u"org.gnome.desktop.background"_s, u"picture-uri"_s})) {
        if (const auto path = parseGnomeWallpaper(*output)) {
            return path;
        }
    }
    QFile plasma(hostConfigHome() + u"/plasma-org.kde.plasma.desktop-appletsrc"_s);
    if (plasma.open(QIODevice::ReadOnly)) {
        if (const auto path = parsePlasmaWallpaper(QString::fromUtf8(plasma.readAll()))) {
            return path;
        }
    }
    if (const auto output = runTool(u"hyprctl"_s, {u"hyprpaper"_s, u"listactive"_s})) {
        if (const auto path = parseHyprpaper(*output)) {
            return path;
        }
    }
    if (const auto output = runTool(u"swww"_s, {u"query"_s})) {
        if (const auto path = parseSwww(*output)) {
            return path;
        }
    }
    return std::nullopt;
}

std::optional<QColor> SystemAppearance::parsePortalAccent(double red, double green, double blue)
{
    const auto valid = [](double c) { return c >= 0.0 && c <= 1.0; };
    if (!valid(red) || !valid(green) || !valid(blue)) {
        return std::nullopt;
    }
    return QColor::fromRgbF(static_cast<float>(red), static_cast<float>(green), static_cast<float>(blue));
}

std::optional<QColor> SystemAppearance::parseGnomeAccent(const QString &gsettingsOutput)
{
    // GNOME 47+ accent palette (libadwaita).
    static const std::pair<QLatin1StringView, QLatin1StringView> kAccents[] = {
        {"blue"_L1, "#3584e4"_L1}, {"teal"_L1, "#2190a4"_L1},   {"green"_L1, "#3a944a"_L1},
        {"yellow"_L1, "#c88800"_L1}, {"orange"_L1, "#ed5b00"_L1}, {"red"_L1, "#e62d42"_L1},
        {"pink"_L1, "#d56199"_L1}, {"purple"_L1, "#9141ac"_L1}, {"slate"_L1, "#6f8396"_L1}};
    const QString name = stripQuotes(gsettingsOutput);
    for (const auto &[accent, hex] : kAccents) {
        if (name == accent) {
            return QColor(hex);
        }
    }
    return std::nullopt;
}

std::optional<QColor> SystemAppearance::parseKdeAccent(const QString &kdeglobals)
{
    bool inGeneral = false;
    const QStringList lines = kdeglobals.split(u'\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(u'[')) {
            inGeneral = line == u"[General]"_s;
            continue;
        }
        if (!inGeneral || !line.startsWith(u"AccentColor="_s)) {
            continue;
        }
        const QStringList parts = line.mid(12).split(u',');
        if (parts.size() < 3) {
            return std::nullopt;
        }
        int rgb[3];
        for (int i = 0; i < 3; ++i) {
            bool ok = false;
            rgb[i] = parts[i].trimmed().toInt(&ok);
            if (!ok || rgb[i] < 0 || rgb[i] > 255) {
                return std::nullopt;
            }
        }
        return QColor(rgb[0], rgb[1], rgb[2]);
    }
    return std::nullopt;
}

std::optional<QString> SystemAppearance::parseGnomeWallpaper(const QString &gsettingsOutput)
{
    return localFile(stripQuotes(gsettingsOutput));
}

std::optional<QString> SystemAppearance::parsePlasmaWallpaper(const QString &appletsrc)
{
    static const QRegularExpression image(u"^\\s*Image=(.+)$"_s, QRegularExpression::MultilineOption);
    auto matches = image.globalMatch(appletsrc);
    while (matches.hasNext()) {
        if (const auto path = localFile(matches.next().captured(1))) {
            return path;
        }
    }
    return std::nullopt;
}

std::optional<QString> SystemAppearance::parseHyprpaper(const QString &listActiveOutput)
{
    // "eDP-1 = /home/user/wall.png"
    for (const QString &line : listActiveOutput.split(u'\n')) {
        const qsizetype equals = line.indexOf(u'=');
        if (equals > 0) {
            if (const auto path = localFile(line.mid(equals + 1))) {
                return path;
            }
        }
    }
    return std::nullopt;
}

std::optional<QString> SystemAppearance::parseSwww(const QString &queryOutput)
{
    // "eDP-1: 1920x1080, scale: 1, currently displaying: image: /home/user/wall.png"
    static const QRegularExpression image(u"image:\\s*(.+)$"_s, QRegularExpression::MultilineOption);
    auto matches = image.globalMatch(queryOutput);
    while (matches.hasNext()) {
        if (const auto path = localFile(matches.next().captured(1))) {
            return path;
        }
    }
    return std::nullopt;
}

} // namespace vedit::theme
