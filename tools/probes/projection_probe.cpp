// SPDX-License-Identifier: GPL-3.0-or-later
// Probe program (SPEC §9 rule 5) for the Phase 1 timeline projection:
//  1. "loader" producers: letterboxing of vertical and rotated videos inside a 16:9 profile (and alpha of the borders);
//  2. tractor with a background track, a playlist of cuts and audio "mix" transitions;
//  3. modifying a playlist while the consumer is playing;
//  4. export with the avformat consumer and progress polling.
// Usage: projection_probe <testmedia dir> <output mp4>
#include <mlt++/Mlt.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace {

int failures = 0;
std::atomic<int> shown{0};

void check(bool condition, const char *what)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FAIL]", what);
    if (!condition) {
        ++failures;
    }
}

void onShow(mlt_properties, void *, mlt_event_data data)
{
    Mlt::Frame frame(mlt_event_data_to_frame(data));
    if (frame.is_valid()) {
        ++shown;
    }
}

struct Pixel
{
    int r, g, b, a;
};

Pixel pixelAt(Mlt::Producer &producer, Mlt::Profile &profile, int position, int x, int y)
{
    producer.seek(position);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    mlt_image_format format = mlt_image_rgba;
    int w = profile.width();
    int h = profile.height();
    const uint8_t *image = frame->get_image(format, w, h);
    if (!image || x >= w || y >= h) {
        return {-1, -1, -1, -1};
    }
    const uint8_t *p = image + (y * w + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

int run(const std::string &dir, const char *output)
{
    Mlt::Factory::init();
    Mlt::Profile profile;
    profile.set_width(1920);
    profile.set_height(1080);
    profile.set_frame_rate(30, 1);
    profile.set_sample_aspect(1, 1);
    profile.set_display_aspect(16, 9);
    profile.set_progressive(1);
    profile.set_colorspace(709);
    profile.set_explicit(1);

    // 1. Loader producers and letterboxing.
    Mlt::Producer vertical(profile, (dir + "/vertical_720x1280.mp4").c_str());
    Mlt::Producer rotated(profile, (dir + "/rotated90_1280x720.mp4").c_str());
    Mlt::Producer landscape(profile, (dir + "/testsrc_720p30.mp4").c_str());
    Mlt::Producer music(profile, (dir + "/music_6s.mp3").c_str());
    Mlt::Producer photo(profile, (dir + "/photo_red.png").c_str());
    check(vertical.is_valid() && rotated.is_valid() && landscape.is_valid() && music.is_valid() && photo.is_valid(),
          "loader opens video, rotated video, mp3 and png");
    std::printf("       services: vertical=%s rotated=%s photo=%s; lengths %d %d %d %d\n", vertical.get("mlt_service"),
                rotated.get("mlt_service"), photo.get("mlt_service"), vertical.get_length(), rotated.get_length(),
                music.get_length(), photo.get_length());
    const Pixel border = pixelAt(vertical, profile, 10, 20, 540);
    const Pixel centre = pixelAt(vertical, profile, 10, 960, 540);
    std::printf("       vertical: border rgba(%d,%d,%d,%d) centre rgba(%d,%d,%d,%d)\n", border.r, border.g, border.b,
                border.a, centre.r, centre.g, centre.b, centre.a);
    check(border.r == 0 && border.g == 0 && border.b == 0, "vertical video is pillarboxed (black left border)");
    const Pixel rotBorder = pixelAt(rotated, profile, 10, 20, 540);
    std::printf("       rotated: border rgba(%d,%d,%d,%d)\n", rotBorder.r, rotBorder.g, rotBorder.b, rotBorder.a);
    check(rotBorder.r == 0 && rotBorder.g == 0 && rotBorder.b == 0, "rotation metadata applied (pillarboxed)");
    const Pixel photoBorder = pixelAt(photo, profile, 0, 20, 540);
    std::printf("       photo 640x480: border rgba(%d,%d,%d,%d)\n", photoBorder.r, photoBorder.g, photoBorder.b, photoBorder.a);

    // 2. Tractor: background + main playlist + audio track, mixed.
    Mlt::Tractor tractor(profile);
    Mlt::Producer background(profile, "color:#000000");
    background.set("length", 100000);
    background.set_in_and_out(0, 99999);
    Mlt::Playlist main(profile);
    main.append(landscape, 0, 59);
    main.append(vertical, 0, 59);
    Mlt::Playlist audio(profile);
    audio.append(music, 0, 89);
    tractor.set_track(background, 0);
    tractor.set_track(main, 1);
    tractor.set_track(audio, 2);
    for (int track = 1; track <= 2; ++track) {
        Mlt::Transition mix(profile, "mix");
        mix.set("always_active", 1);
        mix.set("sum", 1);
        tractor.plant_transition(mix, 0, track);
    }
    std::printf("       tractor length %d (main 120 frames, audio 90)\n", tractor.get_length());
    check(tractor.get_length() >= 120, "tractor length follows the longest track");

    // 3. Live modification while playing.
    Mlt::Consumer consumer(profile, "sdl2_audio");
    consumer.set("volume", 0.0);
    consumer.set("real_time", -2);
    consumer.set("terminate_on_pause", 0);
    consumer.connect(tractor);
    std::unique_ptr<Mlt::Event> event(consumer.listen("consumer-frame-show", nullptr, onShow));
    tractor.set_speed(1.0);
    consumer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const int before = shown.load();
    tractor.lock();
    main.clear();
    main.append(vertical, 30, 89);
    main.append(landscape, 0, 29);
    tractor.unlock();
    consumer.purge();
    tractor.seek(0);
    consumer.set("refresh", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const int after = shown.load() - before;
    consumer.stop();
    std::printf("       frames before edit %d, after edit %d, main playlist count %d\n", before, after, main.count());
    check(before > 5 && after > 10, "playback continues after editing the playlist live");
    event.reset();

    // 4. Export with the avformat consumer.
    Mlt::Consumer encoder(profile, "avformat", output);
    encoder.set("vcodec", "libx264");
    encoder.set("acodec", "aac");
    encoder.set("crf", 23);
    encoder.set("preset", "veryfast");
    encoder.set("real_time", -4);
    encoder.set("terminate_on_pause", 1);
    encoder.set("f", "mp4");
    encoder.set("movflags", "+faststart");
    tractor.set_speed(1.0);
    tractor.seek(0);
    // Stop at the end of the timeline (the background track is much longer).
    tractor.set_in_and_out(0, 89);
    encoder.connect(tractor);
    const auto start = std::chrono::steady_clock::now();
    encoder.start();
    int polls = 0;
    int lastPosition = -1;
    while (!encoder.is_stopped()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const int position = encoder.position();
        if (position != lastPosition) {
            ++polls;
            lastPosition = position;
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("       export: %d distinct progress positions, last %d, %.2f s\n", polls, lastPosition, seconds);
    check(polls >= 2, "progress can be polled during export");
    encoder.stop();
    return failures;
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <testmedia dir> <output.mp4>\n", argv[0]);
        return 2;
    }
    const int result = run(argv[1], argv[2]);
    Mlt::Factory::close();
    std::printf("%s (%d failures)\n", result == 0 ? "ALL OK" : "SOME CHECKS FAILED", result);
    return result == 0 ? 0 : 1;
}
