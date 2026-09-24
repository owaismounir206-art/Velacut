// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditCommand.h"

namespace vedit {

namespace {
constexpr int kEditCommandId = 0x7ed17;
}

EditCommand::EditCommand(Project &project, const QString &text, EditScript script, MergeKey mergeKey)
    : m_project(project)
    , m_script(std::move(script))
    , m_mergeKey(std::move(mergeKey))
{
    setText(text);
}

void EditCommand::redo()
{
    ProjectMutator mutator(m_project);
    for (const auto &edit : m_script) {
        edit->apply(mutator);
    }
}

void EditCommand::undo()
{
    ProjectMutator mutator(m_project);
    for (auto it = m_script.rbegin(); it != m_script.rend(); ++it) {
        (*it)->revert(mutator);
    }
}

int EditCommand::id() const
{
    return m_mergeKey.isEmpty() ? -1 : kEditCommandId;
}

bool EditCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const EditCommand *>(other);
    if (!next || &next->m_project != &m_project || next->m_mergeKey != m_mergeKey || m_mergeKey.isEmpty()) {
        return false;
    }
    // QUndoStack deletes `other` right after a successful merge, so taking its edits is safe.
    auto &nextScript = const_cast<EditCommand *>(next)->m_script;
    // Fast path: same shape and every edit can absorb its successor (e.g. one parameter dragged
    // repeatedly): the merged command stays as small as a single step.
    bool absorbable = nextScript.size() == m_script.size();
    for (size_t i = 0; absorbable && i < m_script.size(); ++i) {
        absorbable = m_script[i]->canAbsorb(*nextScript[i]);
    }
    if (absorbable) {
        for (size_t i = 0; i < m_script.size(); ++i) {
            m_script[i]->absorb(*nextScript[i]);
        }
    } else {
        // Different shapes: applying both scripts in sequence is always correct.
        for (auto &edit : nextScript) {
            m_script.push_back(std::move(edit));
        }
    }
    nextScript.clear();
    return true;
}

} // namespace vedit
