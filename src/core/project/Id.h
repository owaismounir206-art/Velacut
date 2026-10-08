// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHashFunctions>
#include <QString>
#include <QStringView>
#include <QUuid>

#include <functional>
#include <optional>

namespace velacut {

// Stable identifier of a project entity (UUID v4), typed per category so that a ClipId can never be
// passed where a TrackId is expected. Commands always refer to entities by id, never by pointer.
template<typename Tag>
class Id
{
public:
    Id() = default;

    static Id create() { return Id(QUuid::createUuid()); }
    // Accepts the canonical lowercase form without braces (docs/FILE_FORMAT.md §3.1).
    static std::optional<Id> fromString(QStringView text)
    {
        if (text.size() != 36) {
            return std::nullopt;
        }
        const QUuid uuid = QUuid::fromString(text);
        if (uuid.isNull()) {
            return std::nullopt;
        }
        return Id(uuid);
    }

    bool isNull() const noexcept { return m_uuid.isNull(); }
    QString toString() const { return m_uuid.toString(QUuid::WithoutBraces); }
    const QUuid &uuid() const noexcept { return m_uuid; }

    friend bool operator==(const Id &a, const Id &b) noexcept = default;
    friend bool operator<(const Id &a, const Id &b) noexcept { return a.m_uuid < b.m_uuid; }
    friend size_t qHash(const Id &id, size_t seed = 0) noexcept { return qHash(id.m_uuid, seed); }

private:
    explicit Id(const QUuid &uuid)
        : m_uuid(uuid)
    {
    }

    QUuid m_uuid;
};

using ProjectId = Id<struct ProjectTag>;
using MediaId = Id<struct MediaTag>;
using FolderId = Id<struct FolderTag>;
using SequenceId = Id<struct SequenceTag>;
using TrackId = Id<struct TrackTag>;
using ClipId = Id<struct ClipTag>;
using TransitionId = Id<struct TransitionTag>;
using EffectId = Id<struct EffectTag>;
using MarkerId = Id<struct MarkerTag>;
using GroupId = Id<struct GroupTag>;
using LinkId = Id<struct LinkTag>;
using MaskId = Id<struct MaskTag>;

} // namespace velacut

template<typename Tag>
struct std::hash<velacut::Id<Tag>>
{
    size_t operator()(const velacut::Id<Tag> &id) const noexcept { return qHash(id); }
};
