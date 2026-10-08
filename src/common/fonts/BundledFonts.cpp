// SPDX-License-Identifier: GPL-3.0-or-later
#include "BundledFonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include <mutex>

using namespace Qt::StringLiterals;

namespace velacut::fonts {

QString loadBundled()
{
    static std::once_flag once;
    static QString family;
    std::call_once(once, [] {
        const auto load = [](const QString &resource) {
            const QStringList families = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(resource));
            return families.isEmpty() ? QString() : families.constFirst();
        };
        family = load(u":/velacut/fonts/InterVariable.ttf"_s);
        load(u":/velacut/fonts/InterVariable-Italic.woff2"_s);
        if (!family.isEmpty() && family != u"Inter"_s) {
            QFont::insertSubstitution(u"Inter"_s, family);
        }
    });
    return family;
}

} // namespace velacut::fonts
