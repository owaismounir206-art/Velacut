// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fingerprint.h"

#include <QCryptographicHash>
#include <QFile>
#include <QtEndian>

namespace vedit::engine {

namespace {
constexpr qint64 kMiB = 1024 * 1024;
}

std::optional<MediaFingerprint> sampledFingerprint(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return std::nullopt;
    }
    const qint64 size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const quint64 littleEndianSize = qToLittleEndian(static_cast<quint64>(size));
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(&littleEndianSize), sizeof(littleEndianSize)));
    const auto addRange = [&](qint64 offset, qint64 length) {
        if (!file.seek(offset)) {
            return false;
        }
        const QByteArray bytes = file.read(length);
        hash.addData(bytes);
        return bytes.size() == length;
    };
    bool ok = true;
    if (size <= 3 * kMiB) {
        ok = addRange(0, size);
    } else {
        ok = addRange(0, kMiB) && addRange(size / 2, kMiB) && addRange(size - kMiB, kMiB);
    }
    if (!ok) {
        if (error) {
            *error = file.errorString();
        }
        return std::nullopt;
    }
    return MediaFingerprint{QStringLiteral("sha256-sampled-v1"), QString::fromLatin1(hash.result().toHex()), size};
}

} // namespace vedit::engine
