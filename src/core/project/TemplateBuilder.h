// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ProjectData.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace vedit {

// Builds a project from a template manifest (SPEC §5.13, §5.13bis).
class TemplateBuilder
{
public:
    // Creates a project from a template spec (the JSON of templates.json's items[]).
    // Returns the sequence with placeholder clips ready to be filled; the caller adds it to a project.
    static std::optional<Sequence> fromTemplate(const QJsonObject &spec, const Rational &projectRate);

private:
    TemplateBuilder() = default;
};

} // namespace vedit
