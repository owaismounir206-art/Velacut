// SPDX-License-Identifier: GPL-3.0-or-later
// Engine-internal (includes MLT): the preview consumer shared by the media player and the timeline player.
#pragma once

#include <QImage>

#include <mlt++/Mlt.h>

#include <memory>

namespace velacut::engine {

// sdl2_audio (audio to PipeWire/PulseAudio, which also paces the video), parallel rendering with frame
// dropping, RGBA frames. Returns null if no audio output is available.
std::unique_ptr<Mlt::Consumer> createPreviewConsumer(Mlt::Profile &profile, double volume);

// A copy of the frame's pixels as RGBA8888 (null image if the frame has no usable image). Copying is required:
// an MLT frame kept alive after its consumer is closed prevents the consumer from being freed (D-19).
QImage copyFrameImage(Mlt::Frame &frame);

namespace detail {
// Exact mlt_listener signature (mlt_event_data is a struct passed by value).
template<typename Receiver>
void onConsumerFrameShow(mlt_properties, void *owner, mlt_event_data data)
{
    Mlt::Frame frame(mlt_event_data_to_frame(data));
    if (frame.is_valid()) {
        static_cast<Receiver *>(owner)->deliverFrame(frame);
    }
}
} // namespace detail

// Calls `receiver->deliverFrame(Mlt::Frame &)` from the consumer thread for every frame shown. The listener stays
// registered until the consumer is closed, so the consumer must be stopped before the receiver goes away.
template<typename Receiver>
std::unique_ptr<Mlt::Event> listenFrameShow(Mlt::Consumer &consumer, Receiver *receiver)
{
    return std::unique_ptr<Mlt::Event>(
        consumer.listen("consumer-frame-show", receiver, detail::onConsumerFrameShow<Receiver>));
}

} // namespace velacut::engine
