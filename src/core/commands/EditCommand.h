// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/Edit.h"

#include <QUndoCommand>

namespace velacut {

// Identifies a continuous user gesture (slider drag, handle drag) on one target. Commands with the same
// non-empty key merge into a single undo step; a new gesture gets a new gestureId, so two separate drags
// stay two steps (docs/ARCHITECTURE.md §4.4).
struct MergeKey
{
    QString target;
    quint64 gestureId = 0;

    bool isEmpty() const { return target.isEmpty() || gestureId == 0; }
    friend bool operator==(const MergeKey &, const MergeKey &) = default;
};

// The QUndoCommand used for every project modification: a translated text plus an EditScript.
class EditCommand : public QUndoCommand
{
public:
    EditCommand(Project &project, const QString &text, EditScript script, MergeKey mergeKey = {});

    void redo() override;
    void undo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

    const MergeKey &mergeKey() const noexcept { return m_mergeKey; }
    size_t editCount() const noexcept { return m_script.size(); }

private:
    Project &m_project;
    EditScript m_script;
    MergeKey m_mergeKey;
};

} // namespace velacut
