// SPDX-License-Identifier: Apache-2.0
#include "camera.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
static volatile sig_atomic_t stopping;
static std::atomic<unsigned long> frames;
static void stop(int) { stopping = 1; }
static GstPadProbeReturn frame(GstPad *, GstPadProbeInfo *info, gpointer sink) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    for (unsigned i = 0; i < gst_buffer_n_memory(buffer); ++i) {
        if (!gst_is_dmabuf_memory(gst_buffer_peek_memory(buffer, i))) {
            GST_ELEMENT_ERROR(GST_ELEMENT(sink), RESOURCE, FAILED,
                ("Felix did not provide DMA-BUF storage"), (nullptr));
            return GST_PAD_PROBE_DROP;
        }
    }
    ++frames;
    return GST_PAD_PROBE_OK;
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "Usage: inmys-browser-camera SOCKET READY\n"); return 2; }
    const char *device = argv[1];
    const char *ready = argc > 2 ? argv[2] : nullptr;
    GstElement *pipe = nullptr;
    try {
        gst_init(nullptr, nullptr);
        auto camera = select_camera("auto", "", -1);
        GError *error = nullptr;
        std::string launch = "felixsrc name=camera ";
        launch += "! video/x-raw,format=NV12,width=1280,height=720 ! videorate drop-only=true "
                  "! video/x-raw,framerate=15/1 ! unixfdsink name=output sync=false";
        pipe = gst_parse_launch(launch.c_str(), &error);
        if (error || !pipe) {
            std::string reason = error ? error->message : "Cannot create camera pipeline";
            if (error)
                g_error_free(error);
            throw std::runtime_error(reason);
        }
        GstElement *source = gst_bin_get_by_name(GST_BIN(pipe), "camera");
        g_object_set(source, "sensor", camera.sensor.c_str(), "sensor-mode", camera.mode,
                     "setup-file", camera.setup.c_str(), "alloc-buffers", 6, "exposure-auto", TRUE,
                     "awb-enable", TRUE, nullptr);
        gst_object_unref(source);
        GstElement *output = gst_bin_get_by_name(GST_BIN(pipe), "output");
        g_object_set(output, "socket-path", device, nullptr);
        GstPad *pad = gst_element_get_static_pad(output, "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, frame, output, nullptr);
        gst_object_unref(pad);
        gst_object_unref(output);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        GstBus *bus = gst_element_get_bus(pipe);
        bool failed = gst_element_set_state(pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE;
        auto start = std::chrono::steady_clock::now(), last = start;
        unsigned long previous = 0;
        bool announced = false;
        printf("Camera %s -> %s DMA-BUF NV12 1280x720@15\n", camera.sensor.c_str(), device);
        fflush(stdout);
        while (!stopping && !failed) {
            GstMessage *m = gst_bus_timed_pop_filtered(
                bus, GST_SECOND / 5,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (m) {
                if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                    GError *e = nullptr;
                    gchar *debug = nullptr;
                    gst_message_parse_error(m, &e, &debug);
                    fprintf(stderr, "camera: %s (%s)\n", e->message, debug ? debug : "");
                    g_error_free(e);
                    g_free(debug);
                    failed = true;
                }
                gst_message_unref(m);
                break;
            }
            auto now = std::chrono::steady_clock::now();
            if (frames && !announced) {
                auto *browser = getpwnam("browser");
                if (!browser || chown(device, browser->pw_uid, browser->pw_gid) || chmod(device, 0600))
                    throw std::runtime_error("Cannot grant browser access to camera socket");
                if (ready) {
                    FILE *f = fopen(ready, "w");
                    if (!f)
                        throw std::runtime_error("Cannot write camera ready file");
                    fputs("ready\n", f);
                    fclose(f);
                }
                announced = true;
            }
            if (!frames && std::chrono::duration<double>(now - start).count() > 15) {
                fprintf(stderr, "camera: no frames for 15 seconds\n");
                failed = true;
                break;
            }
            double seconds = std::chrono::duration<double>(now - last).count();
            if (seconds >= 5) {
                auto n = frames.load();
                if (n == previous) {
                    fprintf(stderr, "camera: stalled\n");
                    failed = true;
                    break;
                }
                printf("Camera frames=%lu fps=%.1f\n", n, (n - previous) / seconds);
                fflush(stdout);
                previous = n;
                last = now;
            }
        }
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipe);
        return failed ? 1 : 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "camera: %s\n", e.what());
        if (pipe) {
            gst_element_set_state(pipe, GST_STATE_NULL);
            gst_object_unref(pipe);
        }
        return 1;
    }
}
