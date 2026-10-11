// SPDX-License-Identifier: GPL-3.0-or-later
#include "Parallel.h"

#include <atomic>
#include <memory>
#include <vector>

namespace velacut::fx {

namespace {

std::atomic<ParallelRunner> g_runner{nullptr};
std::atomic<int> g_threads{1};
thread_local bool t_inParallelJob = false;

struct Wrapped
{
    ParallelJob job;
    void *cookie;
};

void wrappedJob(int index, int count, void *cookie)
{
    const auto *w = static_cast<const Wrapped *>(cookie);
    const bool outer = t_inParallelJob;
    t_inParallelJob = true;
    w->job(index, count, w->cookie);
    t_inParallelJob = outer;
}

// Per thread: buffers in use are the first `t_scratchUsed` of the pool.
thread_local std::vector<std::unique_ptr<std::vector<std::uint8_t>>> t_scratchPool;
thread_local std::size_t t_scratchUsed = 0;

} // namespace

void setParallelRunner(ParallelRunner runner, int threads)
{
    g_threads = runner ? std::max(1, threads) : 1;
    g_runner = runner;
}

int parallelThreads()
{
    return t_inParallelJob ? 1 : g_threads.load(std::memory_order_relaxed);
}

void detail::runParallel(int jobs, ParallelJob job, void *cookie)
{
    const ParallelRunner runner = g_runner.load(std::memory_order_acquire);
    if (!runner || t_inParallelJob || jobs <= 1) {
        for (int i = 0; i < jobs; ++i) {
            job(i, jobs, cookie);
        }
        return;
    }
    Wrapped wrapped{job, cookie};
    runner(jobs, wrappedJob, &wrapped);
}

Scratch::Scratch(std::size_t bytes)
    : m_size(bytes)
{
    if (t_scratchUsed == t_scratchPool.size()) {
        t_scratchPool.push_back(std::make_unique<std::vector<std::uint8_t>>());
    }
    std::vector<std::uint8_t> &buffer = *t_scratchPool[t_scratchUsed++];
    if (buffer.size() < bytes) {
        buffer.resize(bytes);
    }
    m_data = buffer.data();
}

Scratch::~Scratch()
{
    --t_scratchUsed;
}

} // namespace velacut::fx
