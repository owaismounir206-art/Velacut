// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/ProjectData.h"

#include <QObject>

namespace vedit {

// The live project: the single source of truth of the editor (docs/ARCHITECTURE.md §4).
// Read access is const; every change goes through a ProjectMutator transaction (used by the commands),
// and each outermost transaction emits exactly one changed() signal.
class Project : public QObject
{
    Q_OBJECT

public:
    explicit Project(ProjectData data, QObject *parent = nullptr);

    const ProjectData &data() const noexcept { return m_data; }

signals:
    void changed(const vedit::ChangeSet &changes);

private:
    friend class ProjectMutator;

    ProjectData m_data;
    int m_transactionDepth = 0;
    ChangeSet m_pending;
};

} // namespace vedit
