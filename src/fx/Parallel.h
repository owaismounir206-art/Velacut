// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace velacut::fx {

// Parallel loops of the CPU kernels ("multithread a fasce", docs/ARCHITECTURE.md §6). fx does not own threads: the
// engine installs MLT's slice threads (setParallelRunner); without a runner, or inside a job already running in
// parallel (no nested pools), the loops run on the calling thread. Every kernel gives the same pixels either way.
using ParallelJob = void (*)(int index, int count, void *cookie);
using ParallelRunner = void (*)(int jobs, ParallelJob job, void *cookie);
void setParallelRunner(ParallelRunner runner, int threads);
int parallelThreads();

namespace detail {
void runParallel(int jobs, ParallelJob job, void *cookie);
}

// fn(begin, end) over contiguous parts of [0, count), at least `grain` items each, in parallel; returns when all are
// done.
template<typename Fn>
void parallelFor(int count, Fn &&fn, int grain = 16)
{
    if (count <= 0) {
        return;
    }
    const int jobs = std::clamp(count / std::max(1, grain), 1, parallelThreads());
    if (jobs <= 1) {
        fn(0, count);
        return;
    }
    struct Job
    {
        std::remove_reference_t<Fn> *fn;
        int count;
    } job{&fn, count};
    detail::runParallel(jobs, [](int index, int jobCount, void *cookie) {
        auto *j = static_cast<Job *>(cookie);
        (*j->fn)(static_cast<int>(static_cast<long>(j->count) * index / jobCount),
                 static_cast<int>(static_cast<long>(j->count) * (index + 1) / jobCount));
    }, &job);
}

// A buffer of this thread reused from frame to frame (kernels that need a copy of the image do not allocate one per
// frame). Scoped: buffers are taken and given back in LIFO order; the contents are undefined when taken.
class Scratch
{
public:
    explicit Scratch(std::size_t bytes);
    ~Scratch();
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;

    std::uint8_t *data() const { return m_data; }
    std::size_t size() const { return m_size; }

private:
    std::uint8_t *m_data = nullptr;
    std::size_t m_size = 0;
};

} // namespace velacut::fx
