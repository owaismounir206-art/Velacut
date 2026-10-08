// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-in for velacut-gpuprobe in tests: behaviour chosen per API through environment variables
// FAKE_VULKAN / FAKE_OPENGL / FAKE_VIDEO = ok | crash | hang | fail.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    const char *variable = std::strcmp(mode, "--vulkan") == 0   ? "FAKE_VULKAN"
                           : std::strcmp(mode, "--opengl") == 0 ? "FAKE_OPENGL"
                                                                : "FAKE_VIDEO";
    const char *behaviour = std::getenv(variable);
    const char *what = behaviour ? behaviour : "ok";
    if (std::strcmp(what, "crash") == 0) {
        std::abort();
    }
    if (std::strcmp(what, "hang") == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    if (std::strcmp(what, "fail") == 0) {
        std::fprintf(stderr, "no driver\n");
        return 1;
    }
    if (std::strcmp(mode, "--vulkan") == 0) {
        std::puts(R"({"vulkan":{"status":"ok","devices":[{"name":"Fake GPU","type":"discrete","apiVersion":"1.3.0","deviceLocalMemoryMB":8192}]}})");
    } else if (std::strcmp(mode, "--opengl") == 0) {
        std::puts(R"({"opengl":{"status":"ok","major":4,"minor":6,"version":"4.6 Fake","renderer":"Fake GPU","vendor":"Fake"}})");
    } else {
        std::puts(R"({"video":{"status":"ok","devices":["vaapi"],"encoders":["h264_vaapi"],"decoders":["h264@vaapi"]}})");
    }
    return 0;
}
