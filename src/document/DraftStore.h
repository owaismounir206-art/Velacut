// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Id.h"
#include "core/time/RationalTime.h"

#include <QDateTime>
#include <QList>
#include <QSize>
#include <QString>

#include <memory>
#include <optional>

namespace vedit::document {

class Document;

// What the home screen shows of a draft (from draft.json, regenerated from project.vproj if missing).
struct DraftInfo
{
    ProjectId id;
    QString directory;
    QString name;
    QDateTime modifiedAt;
    RationalTime duration;
    QSize canvas;
    QString thumbnailPath; // empty if none yet
    bool openElsewhere = false;
};

// The drafts folder (~/.local/share/vedit/drafts/<projectId>/, docs/FILE_FORMAT.md §6.1).
class DraftStore
{
public:
    explicit DraftStore(QString root);
    static QString defaultRoot();

    QString root() const { return m_root; }
    QString directoryOf(const ProjectId &id) const;

    // Most recently modified first.
    QList<DraftInfo> list() const;
    std::optional<DraftInfo> info(const ProjectId &id) const;

    // "New project": an empty project with a default name, opened at once (SPEC 0bis rule 1).
    std::unique_ptr<Document> createDraft(QString *error) const;
    std::unique_ptr<Document> openDraft(const ProjectId &id, QString *error) const;

    // Home screen actions on closed drafts.
    bool renameDraft(const ProjectId &id, const QString &name, QString *error) const;
    std::optional<ProjectId> duplicateDraft(const ProjectId &id, QString *error) const;
    // Moves the draft to the system trash (recoverable from the file manager).
    bool removeDraft(const ProjectId &id, QString *error) const;

    // `base`, or "base (2)", "base (3)"… if a draft already has that name.
    QString uniqueName(const QString &base) const;

private:
    QString m_root;
};

} // namespace vedit::document
