// SPDX-License-Identifier: GPL-3.0-or-later
#include "DraftLock.h"

#include "core/serialization/ProjectFile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include <cerrno>
#include <csignal>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace vedit::document {

namespace {

QString lockPath(const QString &directory)
{
    return directory + u"/lock"_s;
}

QString bootId()
{
    QFile file(u"/proc/sys/kernel/random/boot_id"_s);
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll().trimmed()) : QString();
}

// Name of the running program as the kernel reports it (/proc/<pid>/comm keeps 15 characters).
QString programOf(qint64 pid)
{
    QFile file(u"/proc/%1/comm"_s.arg(pid));
    return file.open(QIODevice::ReadOnly) ? QString::fromLocal8Bit(file.readAll().trimmed()) : QString();
}

QString ownProgram()
{
    return programOf(QCoreApplication::applicationPid());
}

bool heldByLiveProcess(const QJsonObject &lock)
{
    const qint64 pid = lock.value(u"pid"_s).toInteger();
    if (pid <= 0 || lock.value(u"bootId"_s).toString() != bootId() ||
        lock.value(u"hostname"_s).toString() != QSysInfo::machineHostName()) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
        return false; // no such process
    }
    // The pid may have been reused by an unrelated program after a crash.
    return programOf(pid) == lock.value(u"program"_s).toString();
}

QJsonObject readLock(const QString &directory)
{
    QFile file(lockPath(directory));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

DraftLock::Outcome DraftLock::acquire(const QString &directory, QString *error)
{
    Outcome outcome = Outcome::Acquired;
    if (QFileInfo::exists(lockPath(directory))) {
        const QJsonObject existing = readLock(directory);
        if (heldByLiveProcess(existing)) {
            if (error) {
                *error = QCoreApplication::translate("vedit::document::DraftLock",
                                                     "This project is already open in another vedit window.");
            }
            return Outcome::HeldElsewhere;
        }
        outcome = Outcome::Recovered;
    }
    const QJsonObject lock{{u"pid"_s, QCoreApplication::applicationPid()},
                           {u"hostname"_s, QSysInfo::machineHostName()},
                           {u"bootId"_s, bootId()},
                           {u"program"_s, ownProgram()},
                           {u"openedAt"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if (const auto written = projectfile::writeAtomically(lockPath(directory), QJsonDocument(lock).toJson()); !written) {
        if (error) {
            *error = written.error;
        }
        return Outcome::Failed;
    }
    return outcome;
}

void DraftLock::release(const QString &directory)
{
    const QJsonObject lock = readLock(directory);
    // Only our own lock (a newer session may have taken over a stale one meanwhile).
    if (lock.value(u"pid"_s).toInteger() == QCoreApplication::applicationPid()) {
        QFile::remove(lockPath(directory));
    }
}

bool DraftLock::isHeld(const QString &directory)
{
    return QFileInfo::exists(lockPath(directory)) && heldByLiveProcess(readLock(directory));
}

} // namespace vedit::document
