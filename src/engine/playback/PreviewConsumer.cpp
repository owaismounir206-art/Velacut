// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreviewConsumer.h"

namespace velacut::engine {

std::unique_ptr<Mlt::Consumer> createPreviewConsumer(Mlt::Profile &profile, double volume)
{
    auto consumer = std::make_unique<Mlt::Consumer>(profile, "sdl2_audio");
    if (!consumer->is_valid()) {
        return nullptr;
    }
    // Frames are rendered by the consumer threads directly in RGBA, the format uploaded by the preview.
    consumer->set("mlt_image_format", "rgba");
    // One read-ahead render thread that drops late frames. Not the parallel workers (real_time < 0 or > 1):
    // their wait for the "rendered" flag checks it outside done_mutex (mlt_consumer.c, worker_get_frame), so a
    // wakeup can be lost; while paused nothing wakes the thread again, the preview freezes and
    // mlt_consumer_stop() deadlocks joining it (reproduced by tst_timelineplayer). Parallelism stays inside
    // FFmpeg's decoders and our own services (sliced kernels).
    consumer->set("real_time", 1);
    consumer->set("terminate_on_pause", 0);
    consumer->set("scrub_audio", 1);
    consumer->set("volume", volume);
    consumer->set("frequency", 48000);
    consumer->set("channels", 2);
    return consumer;
}

QImage copyFrameImage(Mlt::Frame &frame)
{
    mlt_image_format format = mlt_image_rgba;
    int width = 0;
    int height = 0;
    const uint8_t *data = frame.get_image(format, width, height);
    if (!data || format != mlt_image_rgba || width <= 0 || height <= 0) {
        return {};
    }
    return QImage(data, width, height, width * 4, QImage::Format_RGBA8888).copy();
}

} // namespace velacut::engine
