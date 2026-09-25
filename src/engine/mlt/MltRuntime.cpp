// SPDX-License-Identifier: GPL-3.0-or-later
#include "MltRuntime.h"

#include "engine/mlt/Services.h"

#include <QLoggingCategory>

#include <mlt++/Mlt.h>

#include <atomic>
#include <future>
#include <mutex>

Q_LOGGING_CATEGORY(lcMlt, "vedit.engine.mlt")

namespace vedit::engine {

namespace {

std::once_flag s_started;
std::shared_future<Mlt::Repository *> s_repository;
std::atomic<bool> s_ready{false};

void logFromMlt(void *, int level, const char *format, va_list arguments)
{
    if (level > MLT_LOG_WARNING) {
        return;
    }
    char message[1024];
    std::vsnprintf(message, sizeof(message), format, arguments);
    QByteArray text(message);
    text = text.trimmed();
    if (level <= MLT_LOG_ERROR) {
        qCWarning(lcMlt).noquote() << "MLT:" << text;
    } else {
        qCInfo(lcMlt).noquote() << "MLT:" << text;
    }
}

} // namespace

void MltRuntime::initializeAsync()
{
    std::call_once(s_started, [] {
        s_repository = std::async(std::launch::async, [] {
                           mlt_log_set_callback(logFromMlt);
                           Mlt::Repository *repository = Mlt::Factory::init();
                           registerServices(repository);
                           s_ready = repository != nullptr;
                           if (!repository) {
                               qCCritical(lcMlt) << "MLT initialization failed";
                           }
                           return repository;
                       }).share();
    });
}

bool MltRuntime::waitUntilReady()
{
    initializeAsync();
    return s_repository.get() != nullptr;
}

bool MltRuntime::isReady()
{
    return s_ready;
}

Mlt::Repository *MltRuntime::repository()
{
    return waitUntilReady() ? s_repository.get() : nullptr;
}

void MltRuntime::shutdown()
{
    if (s_ready.exchange(false)) {
        Mlt::Factory::close();
    }
}

} // namespace vedit::engine
