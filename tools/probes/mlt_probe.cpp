// SPDX-License-Identifier: GPL-3.0-or-later
// Probe program (SPEC §9 rule 5): observes the real behaviour of the MLT APIs used by the engine.
// Build with -DVELACUT_BUILD_PROBES=ON and run: ./build/tools/probes/mlt_probe <video file>
#include <mlt++/Mlt.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FAIL]", what);
    if (!condition) {
        ++failures;
    }
}

// A custom filter registered at runtime: inverts the red channel of rgba frames.
int invertRed(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable)
{
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    (void) writable;
    if (error == 0 && *format == mlt_image_rgba) {
        const int pixels = *width * *height;
        for (int i = 0; i < pixels; ++i) {
            (*image)[i * 4] = static_cast<uint8_t>(255 - (*image)[i * 4]);
        }
    }
    return error;
}

mlt_frame filterProcess(mlt_filter, mlt_frame frame)
{
    mlt_frame_push_get_image(frame, invertRed);
    return frame;
}

void *createInvert(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (filter) {
        filter->process = filterProcess;
    }
    return filter;
}

std::atomic<int> shownFrames{0};

void onFrameShow(mlt_consumer, Mlt::Consumer *, mlt_event_data data)
{
    Mlt::Frame frame(mlt_event_data_to_frame(data));
    if (frame.is_valid()) {
        ++shownFrames;
    }
}

} // namespace

int run(const char *path)
{
    Mlt::Repository *repository = Mlt::Factory::init();
    check(repository != nullptr, "Mlt::Factory::init");

    // 1. Runtime registration of our own service.
    repository->register_service(mlt_service_filter_type, "vedit.probe_invert", createInvert);

    Mlt::Profile profile;
    Mlt::Producer producer(profile, "avformat", path);
    check(producer.is_valid(), "avformat producer opens the file");
    profile.from_producer(producer);
    std::printf("       profile %dx%d @ %d/%d, length %d frames\n", profile.width(), profile.height(),
                profile.frame_rate_num(), profile.frame_rate_den(), producer.get_length());
    check(profile.frame_rate_num() == 30 && profile.frame_rate_den() == 1, "profile adopts the media frame rate");

    // 2. Frame-accurate RGBA decoding at an arbitrary position.
    producer.seek(45);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    mlt_image_format format = mlt_image_rgba;
    int width = profile.width();
    int height = profile.height();
    const uint8_t *image = frame->get_image(format, width, height);
    check(image != nullptr && format == mlt_image_rgba && width == 1280 && height == 720, "get_image rgba 1280x720");
    check(frame->get_position() == 45, "frame position after seek");
    const uint8_t firstRed = image ? image[0] : 0;

    // 3. Audio of the same frame.
    mlt_audio_format audioFormat = mlt_audio_s16;
    int frequency = 48000;
    int channels = 2;
    int samples = mlt_audio_calculate_frame_samples(30.0f, frequency, 45);
    const void *audio = frame->get_audio(audioFormat, frequency, channels, samples);
    check(audio != nullptr && samples > 0, "get_audio s16");
    std::printf("       audio: %d samples, %d Hz, %d channels\n", samples, frequency, channels);

    // 4. The registered filter is created by name and processes frames.
    Mlt::Filter invert(profile, "vedit.probe_invert");
    check(invert.is_valid(), "runtime-registered filter is created by name");
    producer.attach(invert);
    producer.seek(45);
    std::unique_ptr<Mlt::Frame> filtered(producer.get_frame());
    format = mlt_image_rgba;
    width = profile.width();
    height = profile.height();
    const uint8_t *filteredImage = filtered->get_image(format, width, height);
    check(filteredImage && filteredImage[0] == static_cast<uint8_t>(255 - firstRed), "custom filter applied");
    producer.detach(invert);

    // 5. Tractor + playlist + mix (same-track transition).
    Mlt::Playlist playlist(profile);
    Mlt::Producer *cutA = producer.cut(0, 59);
    Mlt::Producer *cutB = producer.cut(60, 119);
    playlist.append(*cutA);
    playlist.append(*cutB);
    const int before = playlist.get_playtime();
    Mlt::Transition luma(profile, "luma");
    const int mixResult = playlist.mix(0, 10, &luma);
    const int after = playlist.get_playtime();
    std::printf("       playlist playtime %d -> %d after mix(10), result %d, count %d\n", before, after, mixResult,
                playlist.count());
    check(mixResult == 0 && playlist.is_mix(1), "Playlist::mix creates a mix clip");
    delete cutA;
    delete cutB;

    // 6. Real-time consumer with frame-show events (audio muted to stay silent).
    Mlt::Consumer consumer(profile, "sdl2_audio");
    check(consumer.is_valid(), "sdl2_audio consumer");
    if (consumer.is_valid()) {
        consumer.set("volume", 0.0);
        consumer.set("real_time", -2);
        consumer.connect(producer);
        std::unique_ptr<Mlt::Event> event(consumer.listen("consumer-frame-show", nullptr, (mlt_listener) onFrameShow));
        producer.seek(0);
        producer.set_speed(1.0);
        consumer.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        consumer.stop();
        std::printf("       frames shown in 1 s: %d\n", shownFrames.load());
        check(shownFrames.load() >= 20, "consumer-frame-show delivers frames in real time");

        // 7. Start paused (speed 0), then play: what the player does.
        shownFrames = 0;
        producer.set_speed(0.0);
        producer.seek(0);
        consumer.start();
        consumer.set("refresh", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const int pausedFrames = shownFrames.load();
        producer.set_speed(1.0);
        consumer.set("refresh", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::printf("       paused: %d frames in 0.5 s; after play: %d frames in 1 s\n", pausedFrames,
                    shownFrames.load() - pausedFrames);
        check(shownFrames.load() - pausedFrames >= 20, "play after starting paused delivers frames");
        consumer.stop();
    }

    return failures;
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <video file>\n", argv[0]);
        return 2;
    }
    // Every MLT object must be destroyed before Factory::close(), which unloads the modules
    // (destroying a consumer afterwards crashes): run() owns them all.
    const int result = run(argv[1]);
    Mlt::Factory::close();
    std::printf("%s (%d failures)\n", result == 0 ? "ALL OK" : "SOME CHECKS FAILED", result);
    return result == 0 ? 0 : 1;
}
