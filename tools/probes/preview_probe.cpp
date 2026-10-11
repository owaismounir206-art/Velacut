// SPDX-License-Identifier: GPL-3.0-or-later
// Probe (SPEC §9 rule 5): how the paused sdl2_audio preview consumer (real_time = 1, as createPreviewConsumer sets it
// up) schedules renders, to rewrite the seek logic of TimelinePlayer on facts. A slow filter (sleeps N ms in
// get_image, counts its calls) sits on a colour producer whose colour encodes the position.
//
//   E1 paused and idle after the first refresh: how many renders and frames shown in 2 s?
//   E2 seek + purge + refresh while a render is in flight: is the in-flight frame shown, or thrown away?
//   E3 seek + refresh WITHOUT purge, consumer idle: does the new position show, how fast?
//   E4 seek + refresh WITHOUT purge while a render is in flight: which frames are shown?
//   E5 60 seeks per second for 1.5 s, each with purge + refresh (today's scrub): frames shown during the drag.
//   E6 same with "one in flight, last request wins, no purge": frames shown during the drag and after.
//   E7 avformat (--file): same position rendered twice, the second time without seek/purge: is it decoded again?
#include <mlt++/Mlt.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point g_start = Clock::now();

double nowMs()
{
    return std::chrono::duration<double, std::milli>(Clock::now() - g_start).count();
}

std::atomic<int> g_renders{0};
std::atomic<int> g_sleepMs{0};

int slowGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable)
{
    const int position = mlt_frame_pop_service_int(frame);
    const int error = mlt_frame_get_image(frame, image, format, width, height, writable);
    ++g_renders;
    std::printf("    %8.1f ms  render start  pos %d\n", nowMs(), position);
    std::this_thread::sleep_for(std::chrono::milliseconds(g_sleepMs.load()));
    std::printf("    %8.1f ms  render done   pos %d\n", nowMs(), position);
    return error;
}

mlt_frame slowProcess(mlt_filter, mlt_frame frame)
{
    mlt_frame_push_service_int(frame, static_cast<int>(mlt_frame_get_position(frame)));
    mlt_frame_push_get_image(frame, slowGetImage);
    return frame;
}

void *createSlow(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (filter) {
        filter->process = slowProcess;
    }
    return filter;
}

struct Shown
{
    std::mutex mutex;
    std::vector<std::pair<double, int>> frames;
} g_shown;

void onShow(mlt_properties, void *, mlt_event_data data)
{
    Mlt::Frame frame(mlt_event_data_to_frame(data));
    mlt_image_format format = mlt_image_rgba;
    int w = 0;
    int h = 0;
    frame.get_image(format, w, h); // like TimelinePlayer::deliverFrame
    const int position = frame.get_position();
    std::printf("    %8.1f ms  SHOWN         pos %d\n", nowMs(), position);
    const std::lock_guard lock(g_shown.mutex);
    g_shown.frames.emplace_back(nowMs(), position);
}

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

size_t shownCount()
{
    const std::lock_guard lock(g_shown.mutex);
    return g_shown.frames.size();
}

std::pair<double, int> lastShown()
{
    const std::lock_guard lock(g_shown.mutex);
    return g_shown.frames.empty() ? std::pair{-1.0, -1} : g_shown.frames.back();
}

} // namespace

int main(int argc, char **argv)
{
    Mlt::Repository *repository = Mlt::Factory::init();
    repository->register_service(mlt_service_filter_type, "probe.slow", createSlow);
    Mlt::Profile profile("atsc_1080p_30");
    const std::string file = argc > 2 && std::string(argv[1]) == "--file" ? argv[2] : "";

    {
        Mlt::Producer producer(profile, file.empty() ? "color:0x336699ff" : file.c_str());
        Mlt::Filter slow(profile, "probe.slow");
        producer.attach(slow);
        producer.set("length", 100000);
        producer.set("out", 99999);
        Mlt::Consumer consumer(profile, "sdl2_audio");
        consumer.set("mlt_image_format", "rgba");
        consumer.set("real_time", 1);
        consumer.set("terminate_on_pause", 0);
        consumer.set("scrub_audio", 1);
        consumer.set("audio_off", 1);
        // Variants of the read-ahead queue to compare (PROBE_BUFFER, PROBE_PREFILL).
        if (const char *buffer = std::getenv("PROBE_BUFFER")) {
            consumer.set("buffer", std::atoi(buffer));
        }
        if (const char *prefill = std::getenv("PROBE_PREFILL")) {
            consumer.set("prefill", std::atoi(prefill));
        }
        consumer.connect(producer);
        Mlt::Event *event = consumer.listen("consumer-frame-show", nullptr, onShow);
        producer.set_speed(0);
        producer.seek(0);

        g_sleepMs = 50;
        std::printf("E1: start + refresh, then idle 2 s (render 50 ms)\n");
        const int r0 = g_renders;
        consumer.start();
        consumer.set("refresh", 1);
        sleepMs(2000);
        std::printf("E1 result: renders %d, shown %zu\n\n", g_renders - r0, shownCount());

        g_sleepMs = 400;
        std::printf("E2: seek 100 + purge + refresh, after 100 ms seek 200 + purge + refresh (render 400 ms)\n");
        producer.seek(100);
        consumer.purge();
        consumer.set("refresh", 1);
        sleepMs(100);
        producer.seek(200);
        consumer.purge();
        consumer.set("refresh", 1);
        sleepMs(1500);
        std::printf("E2 result: last shown %d at %.0f ms\n\n", lastShown().second, lastShown().first);

        g_sleepMs = 50;
        std::printf("E3: idle, seek 300 + refresh, no purge (render 50 ms)\n");
        const double e3 = nowMs();
        producer.seek(300);
        consumer.set("refresh", 1);
        sleepMs(1000);
        std::printf("E3 result: last shown %d, %.0f ms after the seek\n\n", lastShown().second, lastShown().first - e3);

        g_sleepMs = 400;
        std::printf("E4: seek 400 + refresh, after 100 ms seek 500 + refresh, no purge (render 400 ms)\n");
        producer.seek(400);
        consumer.set("refresh", 1);
        sleepMs(100);
        producer.seek(500);
        consumer.set("refresh", 1);
        sleepMs(1500);
        std::printf("E4 result: last shown %d at %.0f ms\n\n", lastShown().second, lastShown().first);

        g_sleepMs = 150;
        std::printf("E5: 60 seeks/s for 1.5 s with purge + refresh (render 150 ms)\n");
        size_t before = shownCount();
        for (int i = 0; i < 90; ++i) {
            producer.seek(1000 + i);
            consumer.purge();
            consumer.set("refresh", 1);
            sleepMs(16);
        }
        std::printf("E5 result: shown during the drag %zu\n", shownCount() - before);
        sleepMs(1000);
        std::printf("E5 after 1 s: last shown %d\n\n", lastShown().second);

        std::printf("E6: 60 requests/s for 1.5 s, one in flight, last wins, no purge (render 150 ms)\n");
        before = shownCount();
        size_t seen = shownCount();
        bool inFlight = false;
        int pending = -1;
        for (int i = 0; i < 90; ++i) {
            if (shownCount() != seen) {
                seen = shownCount();
                inFlight = false;
            }
            pending = 2000 + i;
            if (!inFlight) {
                producer.seek(pending);
                consumer.set("refresh", 1);
                inFlight = true;
                pending = -1;
            }
            sleepMs(16);
        }
        const double release = nowMs();
        while (pending >= 0 || inFlight) {
            if (shownCount() != seen) {
                seen = shownCount();
                inFlight = false;
            }
            if (!inFlight && pending >= 0) {
                producer.seek(pending);
                consumer.set("refresh", 1);
                inFlight = true;
                pending = -1;
            }
            sleepMs(2);
            if (nowMs() - release > 3000) {
                break;
            }
        }
        std::printf("E6 result: shown during the drag %zu, last shown %d, %.0f ms after the release\n\n",
                    shownCount() - before, lastShown().second, lastShown().first - release);

        if (!file.empty()) {
            g_sleepMs = 0;
            std::printf("E7: avformat, frame 150 shown, then refresh only (no seek), then seek 150 + purge + refresh\n");
            producer.seek(150);
            consumer.purge();
            consumer.set("refresh", 1);
            sleepMs(1000);
            double t = nowMs();
            consumer.set("refresh", 1);
            sleepMs(1000);
            std::printf("E7 refresh only: last shown %d, %.0f ms\n", lastShown().second, lastShown().first - t);
            t = nowMs();
            producer.seek(150);
            consumer.purge();
            consumer.set("refresh", 1);
            sleepMs(1000);
            std::printf("E7 seek same + purge + refresh: last shown %d, %.0f ms\n", lastShown().second, lastShown().first - t);
        }

        consumer.stop();
        delete event;
    }
    if (!file.empty()) {
        // E8: synchronous, no consumer: is a position already decoded served again without decoding?
        Mlt::Producer producer(profile, file.c_str());
        const auto render = [&](int position) {
            const double t = nowMs();
            producer.seek(position);
            Mlt::Frame *frame = producer.get_frame();
            mlt_image_format format = mlt_image_rgba;
            int w = 0;
            int h = 0;
            frame->get_image(format, w, h);
            delete frame;
            return nowMs() - t;
        };
        std::printf("E8: 150 first %.1f ms\n", render(150));
        std::printf("E8: 150 again %.1f ms\n", render(150));
        std::printf("E8: 151 next  %.1f ms\n", render(151));
        std::printf("E8: 150 back  %.1f ms\n", render(150));
        std::printf("E8: 40 far    %.1f ms\n", render(40));
        std::printf("E8: 150 far   %.1f ms\n", render(150));
    }
    Mlt::Factory::close();
    return 0;
}
