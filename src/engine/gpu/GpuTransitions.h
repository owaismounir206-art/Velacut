// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"
#include "fx/Transition.h"

#include <QString>

#include <functional>
#include <memory>

namespace vedit::engine {

// The optional GPU path of the transitions (SPEC §5.11bis "implementazione CPU e, opzionalmente, GPU", 1bis rules 1-4):
// GLSL 1.00/1.10 programs (shaders/transitions.frag), one per transition, run in an offscreen OpenGL 2.1 / ES 2.0
// context on a thread of its own. It is an accelerator only: render() returns false whenever it cannot do the job
// (no context, a shader that does not compile, a driver error, a first self-check that disagrees with the CPU), and
// the caller renders on the CPU. After a failure the GPU path stays off for the session (logged, failure()).
class GpuTransitions
{
public:
    // Creates the process-wide instance; call on the GUI thread (the offscreen surface needs it), once, and only when
    // GPU effects are allowed (graphics decision, not safe mode). Without it, instance() is null.
    static void initialize();
    static void shutdown();
    static GpuTransitions *instance();

    ~GpuTransitions();

    // out = the transition at `progress` (already eased), as fx::renderTransition would draw it. Any thread; jobs are
    // serialized on the GPU thread. False: not done, use the CPU.
    bool render(fx::TransitionKind kind, fx::ImageView out, fx::ConstImageView a, fx::ConstImageView b, double progress,
                const fx::TransitionParams &params);
    bool available() const;
    // Why the GPU path is off ("" while it works).
    QString failure() const;
    // "OpenGL 4.6 · AMD Radeon 740M…" once a context exists.
    QString description() const;
    // Called (on the GPU thread) when the GPU path turns itself off, with the reason: the interface shows a non-blocking
    // notice (SPEC 1bis rule 4).
    void setFailureHandler(std::function<void(const QString &)> handler);

private:
    GpuTransitions();
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace vedit::engine
