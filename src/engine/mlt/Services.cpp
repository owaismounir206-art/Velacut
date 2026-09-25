// SPDX-License-Identifier: GPL-3.0-or-later
#include "Services.h"

#include "fx/Composite.h"

#include <QtGlobal>

#include <mlt++/Mlt.h>

#include <algorithm>

namespace vedit::engine {

namespace {

struct CompositeJob
{
    fx::ImageView destination;
    fx::ConstImageView source;
    int opacity;
};

int compositeSlice(int id, int index, int jobs, void *cookie)
{
    Q_UNUSED(id);
    auto *job = static_cast<CompositeJob *>(cookie);
    const int rows = job->destination.height;
    const int begin = rows * index / jobs;
    const int end = rows * (index + 1) / jobs;
    fx::compositeOver(job->destination, job->source, job->opacity, begin, end);
    return 0;
}

int compositeGetImage(mlt_frame aFrame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    mlt_frame bFrame = mlt_frame_pop_frame(aFrame);
    auto transition = static_cast<mlt_transition>(mlt_frame_pop_service(aFrame));
    *format = mlt_image_rgba;
    int error = mlt_frame_get_image(aFrame, image, format, width, height, 1);
    if (error || *format != mlt_image_rgba || !bFrame || mlt_frame_is_test_card(bFrame)) {
        return error;
    }
    mlt_image_format bFormat = mlt_image_rgba;
    int bWidth = *width;
    int bHeight = *height;
    uint8_t *bImage = nullptr;
    if (mlt_frame_get_image(bFrame, &bImage, &bFormat, &bWidth, &bHeight, 0) != 0 || !bImage ||
        bFormat != mlt_image_rgba || bWidth != *width || bHeight != *height) {
        return 0; // nothing usable to composite: keep the lower tracks
    }
    const mlt_properties properties = MLT_TRANSITION_PROPERTIES(transition);
    CompositeJob job{fx::ImageView{*image, *width, *height, *width * 4},
                     fx::ConstImageView{bImage, bWidth, bHeight, bWidth * 4},
                     mlt_properties_get_int(properties, "opacity_255")};
    if (!mlt_properties_exists(properties, "opacity_255")) {
        job.opacity = 255;
    }
    const int jobs = std::min(mlt_slices_count_normal(), std::max(1, *height / 32));
    mlt_slices_run_normal(jobs, compositeSlice, &job);
    return 0;
}

mlt_frame compositeProcess(mlt_transition transition, mlt_frame aFrame, mlt_frame bFrame)
{
    mlt_frame_push_service(aFrame, transition);
    mlt_frame_push_frame(aFrame, bFrame);
    mlt_frame_push_get_image(aFrame, compositeGetImage);
    return aFrame;
}

void *createComposite(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_transition transition = mlt_transition_new();
    if (transition) {
        transition->process = compositeProcess;
        mlt_properties_set_int(MLT_TRANSITION_PROPERTIES(transition), "_transition_type", 1); // video only
    }
    return transition;
}

} // namespace

void registerServices(Mlt::Repository *repository)
{
    if (!repository) {
        return;
    }
    repository->register_service(mlt_service_transition_type, "vedit.composite", createComposite);
}

} // namespace vedit::engine
