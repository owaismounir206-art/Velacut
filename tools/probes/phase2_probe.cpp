// Phase 2 probe (SPEC §9 rule 5): MLT behaviours the Phase 2 design relies on.
//   phase2_probe <video with audio, 30 fps> <vertical video>
#include <mlt++/Mlt.h>

#include <QCryptographicHash>
#include <QImage>

#include <cmath>
#include <cstdio>
#include <memory>

namespace {

QByteArray frameHash(Mlt::Producer &producer, int position, int width, int height)
{
    producer.seek(position);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    mlt_image_format format = mlt_image_rgba;
    int w = width;
    int h = height;
    const uint8_t *data = frame->get_image(format, w, h);
    if (!data) {
        return "null";
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(data), w * h * 4));
    return hash.result().toHex().left(8);
}

double audioPeak(Mlt::Frame &frame, int fps, int position)
{
    mlt_audio_format format = mlt_audio_s16;
    int frequency = 48000;
    int channels = 2;
    int samples = mlt_audio_calculate_frame_samples(float(fps), frequency, position);
    auto *pcm = static_cast<int16_t *>(frame.get_audio(format, frequency, channels, samples));
    double peak = 0;
    for (int i = 0; pcm && i < samples * channels; ++i) {
        peak = std::max(peak, std::abs(pcm[i]) / 32768.0);
    }
    return peak;
}

// Zero crossings per second of the left channel over one frame: a proxy for the pitch of a sine.
double crossingsPerSecond(Mlt::Frame &frame, int fps, int position)
{
    mlt_audio_format format = mlt_audio_s16;
    int frequency = 48000;
    int channels = 2;
    int samples = mlt_audio_calculate_frame_samples(float(fps), frequency, position);
    auto *pcm = static_cast<int16_t *>(frame.get_audio(format, frequency, channels, samples));
    int crossings = 0;
    for (int i = 1; pcm && i < samples; ++i) {
        if ((pcm[(i - 1) * channels] < 0) != (pcm[i * channels] < 0)) {
            ++crossings;
        }
    }
    return crossings * double(frequency) / samples / 2.0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: phase2_probe <video 30fps with audio> <vertical video>\n");
        return 2;
    }
    Mlt::Factory::init();
    {
        Mlt::Profile profile;
        profile.set_width(320);
        profile.set_height(180);
        profile.set_frame_rate(30, 1);
        profile.set_sample_aspect(1, 1);
        profile.set_display_aspect(16, 9);
        profile.set_progressive(1);
        profile.set_explicit(1);

        Mlt::Producer normal(profile, argv[1]);
        std::printf("normal length %d\n", normal.get_length());

        // 1. timewarp: length, frame mapping, pitch.
        for (const char *spec : {"timewarp:2.0:", "timewarp:0.5:", "timewarp:-1.0:"}) {
            Mlt::Producer warp(profile, (QByteArray(spec) + argv[1]).constData());
            std::printf("%s valid %d length %d warp_speed %s\n", spec, warp.is_valid(), warp.get_length(),
                        warp.get("warp_speed"));
            if (!warp.is_valid()) {
                continue;
            }
            const int n = 10;
            const double speed = warp.get_double("warp_speed");
            const int expected = speed > 0 ? int(std::lround(n * speed)) : normal.get_length() - 1 - n;
            std::printf("  frame %d = %s, normal frame %d = %s\n", n, frameHash(warp, n, 320, 180).constData(), expected,
                        frameHash(normal, expected, 320, 180).constData());
            for (int pitch = 0; pitch <= 1; ++pitch) {
                warp.set("warp_pitch", pitch);
                warp.seek(20);
                std::unique_ptr<Mlt::Frame> frame(warp.get_frame());
                std::printf("  warp_pitch=%d crossings/s %.0f peak %.2f\n", pitch, crossingsPerSecond(*frame, 30, 20),
                            audioPeak(*frame, 30, 20));
            }
        }
        {
            normal.seek(20);
            std::unique_ptr<Mlt::Frame> frame(normal.get_frame());
            std::printf("normal crossings/s %.0f\n", crossingsPerSecond(*frame, 30, 20));
        }

        // 2. playlist repeat: one frame shown N times (freeze).
        {
            Mlt::Playlist playlist(profile);
            playlist.append(normal, 40, 40);
            playlist.repeat(0, 12);
            std::printf("repeat: playtime %d count %d\n", playlist.get_playtime(), playlist.count());
            Mlt::Producer p(playlist);
            std::printf("  frames 0/5/11 = %s %s %s, source 40 = %s\n", frameHash(p, 0, 320, 180).constData(),
                        frameHash(p, 5, 320, 180).constData(), frameHash(p, 11, 320, 180).constData(),
                        frameHash(normal, 40, 320, 180).constData());
        }

        // 3. requesting the source image at its own aspect from the loader: borders?
        {
            Mlt::Producer vertical(profile, argv[2]);
            vertical.seek(5);
            std::unique_ptr<Mlt::Frame> frame(vertical.get_frame());
            mlt_image_format format = mlt_image_rgba;
            int w = 90;
            int h = 160;
            const uint8_t *data = frame->get_image(format, w, h);
            std::printf("vertical at 90x160: got %dx%d, corner alpha %d, centre alpha %d, meta %sx%s\n", w, h, data[3],
                        data[(h / 2 * w + w / 2) * 4 + 3], frame->get("meta.media.width"), frame->get("meta.media.height"));
            Mlt::Producer v2(profile, argv[2]);
            v2.seek(5);
            std::unique_ptr<Mlt::Frame> f2(v2.get_frame());
            int w2 = 320, h2 = 180;
            format = mlt_image_rgba;
            const uint8_t *d2 = f2->get_image(format, w2, h2);
            std::printf("vertical at 320x180: got %dx%d, corner alpha %d\n", w2, h2, d2[3]);
        }

        // 4. a tractor (two tracks + transition) used as a producer inside a playlist.
        {
            Mlt::Tractor mini(profile);
            Mlt::Playlist a(profile);
            Mlt::Playlist b(profile);
            a.append(normal, 0, 29);
            b.append(normal, 60, 89);
            mini.set_track(a, 0);
            mini.set_track(b, 1);
            Mlt::Transition luma(profile, "luma");
            mini.plant_transition(luma, 0, 1);
            Mlt::Playlist outer(profile);
            outer.blank(9);
            outer.append(mini, 0, 29);
            std::printf("mini tractor in playlist: playtime %d, clip 1 length %d\n", outer.get_playtime(),
                        outer.clip_length(1));
            Mlt::Producer p(outer);
            std::printf("  outer frame 12 = %s (mini frame 2 = %s)\n", frameHash(p, 12, 320, 180).constData(),
                        frameHash(mini, 2, 320, 180).constData());
        }
    }
    Mlt::Factory::close();
    return 0;
}
