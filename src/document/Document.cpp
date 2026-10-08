// SPDX-License-Identifier: GPL-3.0-or-later
#include "Document.h"

#include "core/commands/EditCommand.h"
#include "core/serialization/ProjectFile.h"
#include "document/AutoSaver.h"
#include "document/DraftLock.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDocument, "velacut.document")

using namespace Qt::StringLiterals;

namespace velacut::document {

Document::Document(QString directory, ProjectData data)
    : m_directory(std::move(directory))
    , m_project(std::make_unique<Project>(std::move(data)))
{
    m_saver = std::make_unique<AutoSaver>(*m_project, m_directory);
    connect(m_saver.get(), &AutoSaver::stateChanged, this, &Document::saveStateChanged);
    QFile state(m_directory + u"/state.json"_s);
    if (state.open(QIODevice::ReadOnly)) {
        m_uiState = QJsonDocument::fromJson(state.readAll()).object();
    }
}

Document::~Document()
{
    close();
}

std::unique_ptr<Document> Document::open(const QString &directory, QString *error)
{
    QString lockError;
    const DraftLock::Outcome lock = DraftLock::acquire(directory, &lockError);
    if (lock == DraftLock::Outcome::HeldElsewhere || lock == DraftLock::Outcome::Failed) {
        if (error) {
            *error = lockError;
        }
        return nullptr;
    }
    ProjectLoadResult loaded = projectfile::load(directory + u"/project.vproj"_s);
    if (!loaded.ok()) {
        DraftLock::release(directory);
        if (error) {
            *error = loaded.error;
        }
        return nullptr;
    }
    std::unique_ptr<Document> document(new Document(directory, std::move(*loaded.project)));
    document->m_recovered = lock == DraftLock::Outcome::Recovered;
    document->m_loadWarnings = loaded.warnings;
    for (const QString &warning : std::as_const(loaded.warnings)) {
        qCWarning(lcDocument) << directory << warning;
    }
    if (document->m_recovered) {
        qCInfo(lcDocument) << "recovered after an abnormal end:" << directory;
    }
    return document;
}

std::unique_ptr<Document> Document::create(const QString &directory, ProjectData data, QString *error)
{
    if (!QDir().mkpath(directory)) {
        if (error) {
            *error = tr("Cannot create the folder %1.").arg(directory);
        }
        return nullptr;
    }
    if (const auto written = projectfile::save(directory + u"/project.vproj"_s, data); !written) {
        if (error) {
            *error = written.error;
        }
        return nullptr;
    }
    return open(directory, error);
}

bool Document::apply(EditResult edit, MergeKey mergeKey)
{
    if (!edit.ok()) {
        qCWarning(lcDocument) << "edit refused:" << edit.error;
        return false;
    }
    m_undoStack.push(new EditCommand(*m_project, edit.text, std::move(edit.script), std::move(mergeKey)));
    return true;
}

Document::SaveState Document::saveState() const
{
    switch (m_saver->state()) {
    case AutoSaver::State::Saved:
        return SaveState::Saved;
    case AutoSaver::State::Pending:
    case AutoSaver::State::Saving:
        return SaveState::Saving;
    case AutoSaver::State::Failed:
        break;
    }
    return SaveState::Failed;
}

QString Document::saveError() const
{
    return m_saver->error();
}

int Document::writeCount() const
{
    return m_saver->writeCount();
}

void Document::saveNow()
{
    m_saver->saveNow();
}

void Document::setThumbnail(const QImage &image)
{
    m_saver->setThumbnail(image);
}

void Document::setUiState(const QJsonObject &state)
{
    m_uiState = state;
    m_saver->setUiState(QJsonDocument(state).toJson(QJsonDocument::Indented));
}

bool Document::close(QString *error)
{
    if (m_closed) {
        return true;
    }
    m_closed = true;
    const projectfile::WriteResult result = m_saver->flush();
    if (!result) {
        qCWarning(lcDocument) << "closing with unsaved changes:" << result.error;
        if (error) {
            *error = result.error;
        }
        return false;
    }
    DraftLock::release(m_directory);
    return true;
}

} // namespace velacut::document
