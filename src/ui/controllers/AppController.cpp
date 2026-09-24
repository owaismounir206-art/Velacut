// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJSEngine>
#include <QSysInfo>

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {
AppController *s_instance = nullptr;
}

AppController::AppController(engine::Player *player, gpu::GraphicsDecision decision, gpu::GpuCapabilities capabilities,
                             QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_decision(std::move(decision))
    , m_capabilities(std::move(capabilities))
{
}

void AppController::setInstance(AppController *instance)
{
    s_instance = instance;
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "AppController::create", "AppController::setInstance() must be called before QML loads");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString AppController::version() const
{
    return QCoreApplication::applicationVersion();
}

QString AppController::systemInformation() const
{
    QStringList lines;
    lines << u"vedit %1, Qt %2, %3 (%4)"_s.arg(version(), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(),
                                             QSysInfo::kernelVersion());
    lines << u"UI backend: %1%2"_s.arg(uiBackend(), m_decision.safeMode ? u" (safe mode)"_s : QString());
    lines << u"GPU effects: %1, hardware decoding: %2, hardware encoding: %3"_s.arg(
        m_decision.gpuEffects ? u"on"_s : u"off"_s, m_decision.hardwareDecoding ? u"on"_s : u"off"_s,
        m_decision.hardwareEncoding ? u"on"_s : u"off"_s);
    for (const QString &reason : m_decision.reasons) {
        lines << u"  - "_s + reason;
    }
    lines << m_capabilities.summary();
    return lines.join(u'\n');
}

void AppController::updateGraphics(const gpu::GraphicsDecision &decision, const gpu::GpuCapabilities &capabilities)
{
    const gpu::UiBackend ui = m_decision.ui;
    m_decision = decision;
    m_decision.ui = ui;
    m_capabilities = capabilities;
    emit systemInformationChanged();
}

void AppController::copySystemInformation() const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(systemInformation());
    }
}

} // namespace vedit::ui
