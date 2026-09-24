// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/gpu/GraphicsSetup.h"
#include "engine/playback/Player.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace vedit::ui {

// The QML singleton `App` (import Vedit.UI): application-level state and services for the UI.
class AppController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(vedit::engine::Player *player READ player CONSTANT FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString uiBackend READ uiBackend CONSTANT FINAL)
    Q_PROPERTY(bool softwareRendering READ softwareRendering CONSTANT FINAL)
    Q_PROPERTY(bool safeMode READ safeMode CONSTANT FINAL)
    Q_PROPERTY(QStringList graphicsReasons READ graphicsReasons CONSTANT FINAL)
    Q_PROPERTY(QString systemInformation READ systemInformation NOTIFY systemInformationChanged FINAL)

public:
    AppController(engine::Player *player, gpu::GraphicsDecision decision, gpu::GpuCapabilities capabilities,
                  QObject *parent = nullptr);

    static void setInstance(AppController *instance);
    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    engine::Player *player() const { return m_player; }
    QString version() const;
    QString uiBackend() const { return gpu::uiBackendName(m_decision.ui); }
    bool softwareRendering() const { return m_decision.ui == gpu::UiBackend::Software; }
    bool safeMode() const { return m_decision.safeMode; }
    QStringList graphicsReasons() const { return m_decision.reasons; }
    QString systemInformation() const;
    // Updated when the (slower) video probe finishes in background; the UI backend never changes at runtime.
    void updateGraphics(const gpu::GraphicsDecision &decision, const gpu::GpuCapabilities &capabilities);

    // "Copia informazioni di sistema" (SPEC 1bis rule 6).
    Q_INVOKABLE void copySystemInformation() const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }

signals:
    void systemInformationChanged();

private:
    engine::Player *m_player;
    gpu::GraphicsDecision m_decision;
    gpu::GpuCapabilities m_capabilities;
};

} // namespace vedit::ui
