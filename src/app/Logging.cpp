// SPDX-License-Identifier: GPL-3.0-or-later
#include "Logging.h"

#include "common/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace vedit::logging {

namespace {

constexpr qint64 kMaxLogSize = 5 * 1024 * 1024;
QMutex s_mutex;
QFile *s_file = nullptr;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "?";
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QByteArray line = QStringLiteral("%1 %2 [%3] %4\n")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                     QLatin1StringView(levelName(type)),
                                     QLatin1StringView(context.category ? context.category : "default"), message)
                                .toUtf8();
    QMutexLocker lock(&s_mutex);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    if (s_file) {
        s_file->write(line);
        s_file->flush();
    }
}

} // namespace

QString logFilePath()
{
    return paths::stateDir() + u"/logs/vedit.log"_s;
}

void install()
{
    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo(path).size() > kMaxLogSize) {
        QFile::remove(path + u".1"_s);
        QFile::rename(path, path + u".1"_s);
    }
    auto *file = new QFile(path);
    if (file->open(QIODevice::Append | QIODevice::Text)) {
        s_file = file;
    } else {
        delete file;
    }
    qInstallMessageHandler(handler);
}

} // namespace vedit::logging
