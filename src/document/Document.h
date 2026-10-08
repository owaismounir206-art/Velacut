// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/EditCommand.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/Project.h"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QUndoStack>

#include <memory>

namespace velacut::document {

class AutoSaver;

// An open draft: the live project, its undo history and its continuous save (SPEC 0bis rule 2). There is no
// "Save": every command is written within ~2 s, and close() writes the rest.
class Document : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SaveState saveState READ saveState NOTIFY saveStateChanged FINAL)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveStateChanged FINAL)

public:
    enum class SaveState
    {
        Saved,
        Saving, // changes not yet on disk
        Failed, // retried automatically; the project in memory is intact
    };
    Q_ENUM(SaveState)

    // Opens the draft in `directory` (locks it). Null with `error` set if it cannot be opened.
    static std::unique_ptr<Document> open(const QString &directory, QString *error);
    // Creates a new draft in `directory` with `data` and opens it.
    static std::unique_ptr<Document> create(const QString &directory, ProjectData data, QString *error);
    ~Document() override;

    Project &project() { return *m_project; }
    const ProjectData &data() const { return m_project->data(); }
    QUndoStack &undoStack() { return m_undoStack; }
    QString directory() const { return m_directory; }
    // The previous session ended abnormally (the project is at its last save, at most ~2 s lost).
    bool recovered() const { return m_recovered; }
    // Values corrected while loading (for the log).
    QStringList loadWarnings() const { return m_loadWarnings; }

    // Pushes one undoable command; false (nothing changed) if the edit failed. Commands with the same non-empty
    // merge key become one undo step (a gesture, an import of several files).
    bool apply(EditResult edit, MergeKey mergeKey = {});

    SaveState saveState() const;
    QString saveError() const;
    // Number of writes of project.vproj since opening (tests, diagnostics).
    int writeCount() const;
    void saveNow();

    void setThumbnail(const QImage &image);
    // Interface state kept with the draft (playhead, zoom, last export settings…): state.json.
    QJsonObject uiState() const { return m_uiState; }
    void setUiState(const QJsonObject &state);

    // Writes everything and releases the lock. Returns false (with error) if the last write failed: the lock is
    // then kept, so the next opening reports a recovery.
    bool close(QString *error = nullptr);

signals:
    void saveStateChanged();

private:
    Document(QString directory, ProjectData data);

    QString m_directory;
    std::unique_ptr<Project> m_project;
    QUndoStack m_undoStack;
    std::unique_ptr<AutoSaver> m_saver;
    QJsonObject m_uiState;
    QStringList m_loadWarnings;
    bool m_recovered = false;
    bool m_closed = false;
};

} // namespace velacut::document
