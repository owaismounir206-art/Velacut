// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <fcntl.h>
#include <unistd.h>

namespace vedit::projectfile {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("vedit::projectfile", text);
}

bool syncDirectory(const QString &directory)
{
    const int fd = ::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
}

} // namespace

WriteResult writeAtomically(const QString &path, const QByteArray &bytes)
{
    WriteResult result;
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        result.error = tr("Cannot create the folder %1.").arg(info.absolutePath());
        return result;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = tr("Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        result.error = tr("Cannot write %1: %2").arg(path, file.errorString());
        file.cancelWriting();
        return result;
    }
    if (::fsync(file.handle()) != 0) {
        result.error = tr("Cannot write %1: the data could not be flushed to disk.").arg(path);
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.error = tr("Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }
    // Make the rename itself durable. Failure here does not corrupt anything, so it is not an error.
    syncDirectory(info.absolutePath());
    result.ok = true;
    return result;
}

WriteResult save(const QString &path, const ProjectData &project)
{
    return writeAtomically(path, projectjson::toBytes(project));
}

ProjectLoadResult load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ProjectLoadResult result;
        result.error = tr("Cannot open %1: %2").arg(path, file.errorString());
        return result;
    }
    return projectjson::fromBytes(file.readAll());
}

} // namespace vedit::projectfile
