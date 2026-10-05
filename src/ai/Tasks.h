// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/AiTask.h"
#include "ai/Analysis.h"

#include <vector>

namespace vedit::ai {

// "Remove pauses": the quiet stretches of a media file's sound (its own time).
class PauseDetection : public AiTask
{
    Q_OBJECT

public:
    explicit PauseDetection(QString path, QObject *parent = nullptr);
    QString title() const override;
    const std::vector<SourceRange> &pauses() const { return m_pauses; }

protected:
    QString run() override;

private:
    QString m_path;
    std::vector<SourceRange> m_pauses;
};

// "Split scenes": where a video changes shot (seconds of the file).
class SceneDetection : public AiTask
{
    Q_OBJECT

public:
    explicit SceneDetection(QString path, QObject *parent = nullptr);
    QString title() const override;
    const std::vector<double> &cuts() const { return m_cuts; }

protected:
    QString run() override;

private:
    QString m_path;
    std::vector<double> m_cuts;
};

} // namespace vedit::ai
