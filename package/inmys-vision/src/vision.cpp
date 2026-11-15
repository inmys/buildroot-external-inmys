// SPDX-License-Identifier: Apache-2.0
#include "web.hpp"
#include "camera.hpp"
#include "rtsp.hpp"
#include "yolo.hpp"
#include <gst/app/gstappsink.h>
#include <gst/video/video-event.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <glob.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int) { stopping = 1; }
struct Options {
    std::string sensor = "auto", setup;
    unsigned mode = 6, width = 1280, height = 720, fps = 15, bitrate = 4000;
    unsigned http = 8081, rtsp = 8554, seconds = 0;
    std::string display = "auto";
    bool test = false;
    bool yolo = true, mode_set = false;
    bool software_encoding = false;
    std::string image, model = "/usr/share/inmys/yolov8n", statistics;
};
static unsigned number(const char *value, unsigned lo, unsigned hi) {
    char *end = nullptr; unsigned long n = strtoul(value, &end, 10);
    if (!*value || *end || n < lo || n > hi) throw std::runtime_error("invalid numeric argument");
    return static_cast<unsigned>(n);
}
struct Display {
    std::string name = "off";
    int connector = -1;
    unsigned width = 0, height = 0;
};
static Display select_display(const std::string &requested, unsigned width, unsigned height) {
    if (requested == "none") return {};
    // One display controller: prefer HDMI when connected, otherwise the LVDS panel.
    for (const char *type : {"HDMI", "LVDS"}) {
        if (requested != "auto" && requested != (std::string(type) == "HDMI" ? "hdmi" : "lvds")) continue;
        glob_t paths{};
        std::string pattern = std::string("/sys/class/drm/card*-") + type + "-*/status";
        Display selected;
        if (!glob(pattern.c_str(),0,nullptr,&paths))
            for (size_t i = 0; i < paths.gl_pathc; ++i) {
                std::string path = paths.gl_pathv[i], status;
                std::ifstream(path) >> status;
                if (status != "connected") continue;
                path.resize(path.size() - std::string("status").size());
                int id = -1; std::ifstream(path + "connector_id") >> id;
                if (id < 0) continue;
                std::ifstream modes(path + "modes"); std::string mode;
                unsigned w = 0, h = 0, chosen_w = 0, chosen_h = 0;
                while (modes >> mode) {
                    if (sscanf(mode.c_str(),"%ux%u",&w,&h) != 2 || !w || !h || (w & 1) || (h & 1)) continue;
                    if (!chosen_w) { chosen_w = w; chosen_h = h; }
                    if (w == width && h == height) { chosen_w = w; chosen_h = h; break; }
                }
                if (!chosen_w) continue;
                selected = {path.substr(path.find_last_of('/',path.size()-2)+1),id,chosen_w,chosen_h};
                selected.name.pop_back();
                break;
            }
        globfree(&paths);
        if (selected.connector >= 0) return selected;
    }
    if (requested != "auto") throw std::runtime_error("No connected " + requested + " display with a usable mode");
    return {};
}
struct Capture {
    Rtsp &rtsp; Web &web; GstElement *encoder;
    std::atomic<uint64_t> encoded{0}, jpeg{0};
    unsigned fps;
};
static GstPadProbeReturn detect_frame(GstPad *pad, GstPadProbeInfo *probe, gpointer data) {
    auto &yolo = *static_cast<YoloDSP *>(data);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GstVideoInfo info{};
    bool valid = caps && gst_video_info_from_caps(&info,caps) &&
        GST_VIDEO_INFO_FORMAT(&info) == GST_VIDEO_FORMAT_NV12;
    if (caps) gst_caps_unref(caps);
    if (!valid) return GST_PAD_PROBE_DROP;
    // Isolate writable pixels from Felix's buffer pool and from inference input.
    GstBuffer *original = GST_PAD_PROBE_INFO_BUFFER(probe);
    GstBuffer *copy = gst_buffer_copy_deep(original);
    if (!copy) return GST_PAD_PROBE_DROP;
    GST_PAD_PROBE_INFO_DATA(probe) = copy;
    gst_buffer_unref(original);
    GstVideoFrame frame{};
    if (!gst_video_frame_map(&frame,&info,copy,GST_MAP_READWRITE)) return GST_PAD_PROBE_DROP;
    yolo.submit(frame);
    draw_detections(frame,yolo.snapshot());
    gst_video_frame_unmap(&frame);
    return GST_PAD_PROBE_OK;
}
static GstFlowReturn encoded_sample(GstAppSink *sink, gpointer data) {
    auto &c = *static_cast<Capture *>(data);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    GstBuffer *buffer = gst_sample_get_buffer(sample); GstMapInfo map{};
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        auto n = c.encoded.fetch_add(1);
        uint64_t pts = GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer)
                      : gst_util_uint64_scale(n, GST_SECOND, c.fps);
        c.rtsp.push(std::vector<unsigned char>(map.data, map.data + map.size),
                    !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT), pts);
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    if (c.rtsp.need_idr())
        gst_element_send_event(c.encoder, gst_video_event_new_upstream_force_key_unit(
            GST_CLOCK_TIME_NONE, TRUE, 0));
    return GST_FLOW_OK;
}
static GstFlowReturn jpeg_sample(GstAppSink *sink, gpointer data) {
    auto &c = *static_cast<Capture *>(data);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    GstMapInfo map{}; GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        c.web.push(std::vector<unsigned char>(map.data, map.data + map.size));
        ++c.jpeg; gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample); return GST_FLOW_OK;
}
int main(int argc, char **argv) {
    try {
        Options o;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help" || a == "-h") {
                puts("inmys-vision [--sensor auto|imx219|ov5647|imx477] [--setup FILE] [--mode N]\n"
                     "  [--width 1280 --height 720 --fps 15 --bitrate 4000]\n"
                     "  [--http-port 8081 --rtsp-port 8554] [--display auto|hdmi|lvds|none]\n"
                     "  [--no-display | --no-hdmi] (disable local display)\n"
                     "  [--seconds N] [--test-pattern | --test-image FILE]\n"
                     "  [--no-yolo] [--model-dir DIR] [--dsp-statistics FILE]\n"
                     "  [--software-encoding] (default: Mali-V61 H.264/MJPEG)\n"
                     "YOLOv8n runs on ELcore50 DSP 0; CPU inference fallback is disabled.\n"
                     "Display auto: HDMI when connected, otherwise LVDS; selected at startup.\n"
                     "RTSP: /live, HTTP: /video.mjpg");
                return 0;
            }
            if (a == "--no-hdmi" || a == "--no-display") { o.display = "none"; continue; }
            if (a == "--test-pattern") { o.test = true; continue; }
            if (a == "--no-yolo") { o.yolo = false; continue; }
            if (a == "--software-encoding") { o.software_encoding = true; continue; }
            if (++i == argc) throw std::runtime_error("missing option value: " + a);
            if (a == "--display") o.display = argv[i];
            else if (a == "--sensor") o.sensor = argv[i];
            else if (a == "--test-image") o.image = argv[i];
            else if (a == "--model-dir") o.model = argv[i];
            else if (a == "--dsp-statistics") o.statistics = argv[i];
            else if (a == "--setup") o.setup = argv[i];
            else if (a == "--mode") { o.mode = number(argv[i], 0, 32); o.mode_set = true; }
            else if (a == "--width") o.width = number(argv[i], 128, 4096);
            else if (a == "--height") o.height = number(argv[i], 128, 3072);
            else if (a == "--fps") o.fps = number(argv[i], 1, 60);
            else if (a == "--bitrate") o.bitrate = number(argv[i], 100, 50000);
            else if (a == "--http-port") o.http = number(argv[i], 1024, 65535);
            else if (a == "--rtsp-port") o.rtsp = number(argv[i], 1024, 65535);
            else if (a == "--seconds") o.seconds = number(argv[i], 1, 86400);
            else throw std::runtime_error("unknown option: " + a);
        }
        if ((o.width & 1) || (o.height & 1)) throw std::runtime_error("NV12 dimensions must be even");
        if (o.display != "auto" && o.display != "hdmi" && o.display != "lvds" && o.display != "none")
            throw std::runtime_error("--display must be auto, hdmi, lvds or none");
        const Display display = select_display(o.display,o.width,o.height);
        if (!o.test && o.image.empty()) {
            auto selected = select_camera(o.sensor,o.setup,o.mode_set ? static_cast<int>(o.mode) : -1);
            o.sensor = selected.sensor; o.setup = selected.setup; o.mode = selected.mode;
        }
        gst_init(nullptr, nullptr);
        if (!o.software_encoding) {
            if (access("/dev/mv500", F_OK))
                throw std::runtime_error("VPU /dev/mv500 is missing; load mve_rsrc/mve_base or select --software-encoding");
            for (const char *name : {"omxh264enc", "omxmjpegenc"}) {
                auto *factory = gst_element_factory_find(name);
                if (!factory) throw std::runtime_error(std::string("Missing VPU plugin: ") + name);
                gst_object_unref(factory);
            }
        }
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        std::unique_ptr<YoloDSP> yolo;
        if (o.yolo) yolo = std::make_unique<YoloDSP>(o.model,o.statistics);
        Rtsp rtsp(static_cast<int>(o.rtsp), o.fps); Web web(static_cast<int>(o.http), static_cast<int>(o.rtsp));
        std::string h264 = o.software_encoding ?
            "x264enc name=encoder tune=zerolatency speed-preset=ultrafast threads=2 bitrate=" +
            std::to_string(o.bitrate) + " key-int-max=" + std::to_string(o.fps) + " byte-stream=true" :
            "omxh264enc name=encoder control-rate=constant target-bitrate=" +
            std::to_string(o.bitrate * 1000) + " interval-intraframes=" + std::to_string(o.fps - 1) + " b-frames=0";
        std::string mjpeg = o.software_encoding ? "jpegenc quality=75" : "omxmjpegenc qfactor=75";
        std::string pipeline = o.test ? "videotestsrc name=camera is-live=true " : "felixsrc name=camera ";
        if (!o.image.empty()) pipeline = "filesrc name=camera ! jpegdec ! imagefreeze is-live=true ! videoscale ! videoconvert ";
        pipeline += "! video/x-raw,format=NV12,width=" + std::to_string(o.width) +
            ",height=" + std::to_string(o.height) +
            " ! videorate drop-only=true ! video/x-raw,framerate=" + std::to_string(o.fps) + "/1 ! identity name=detector ! tee name=t "
            "t. ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream "
            "! " + h264 + " ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au "
            "! appsink name=encoded emit-signals=true sync=false max-buffers=2 drop=true "
            "t. ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream "
            "! videoscale ! videorate drop-only=true ! video/x-raw,width=640,height=360,framerate=5/1 "
            "! videoconvert ! " + mjpeg + " "
            "! appsink name=jpeg emit-signals=true sync=false max-buffers=1 drop=true ";
        if (display.connector >= 0) {
            pipeline += "t. ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream ";
            if (display.width != o.width || display.height != o.height)
                pipeline += "! videoscale add-borders=true ! video/x-raw,width=" + std::to_string(display.width) +
                    ",height=" + std::to_string(display.height) + ",pixel-aspect-ratio=1/1 ";
            pipeline += "! kmssink driver-name=mali-dp sync=false force-modesetting=true connector-id=" +
                std::to_string(display.connector);
        }
        printf("Source: %s mode=%u output=%ux%u@%u display=%s connector=%d mode=%ux%u\n", !o.image.empty() ? o.image.c_str() : o.test ? "test pattern" : o.sensor.c_str(),
               o.mode, o.width, o.height, o.fps, display.name.c_str(), display.connector, display.width, display.height); fflush(stdout);
        printf("Encoding: %s (H.264 and MJPEG)\n", o.software_encoding ? "CPU" : "Mali-V61 VPU"); fflush(stdout);
        GError *error = nullptr; GstElement *pipe = gst_parse_launch(pipeline.c_str(), &error);
        if (error || !pipe) {
            std::string reason = error ? error->message : "cannot create pipeline";
            if (error) g_error_free(error);
            if (pipe) gst_object_unref(pipe);
            throw std::runtime_error(reason);
        }
        GstElement *camera = gst_bin_get_by_name(GST_BIN(pipe), "camera");
        if (!o.image.empty()) g_object_set(camera,"location",o.image.c_str(),nullptr);
        else if (!o.test) g_object_set(camera, "sensor", o.sensor.c_str(), "sensor-mode", o.mode,
            "setup-file", o.setup.c_str(), "alloc-buffers", 6, "exposure-auto", TRUE,
            "awb-enable", TRUE, nullptr);
        gst_object_unref(camera);
        if (yolo) {
            GstElement *detector = gst_bin_get_by_name(GST_BIN(pipe),"detector");
            GstPad *pad = gst_element_get_static_pad(detector,"src");
            gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,detect_frame,yolo.get(),nullptr);
            gst_object_unref(pad); gst_object_unref(detector);
        }
        GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipe), "encoder");
        Capture capture{rtsp, web, encoder, {0}, {0}, o.fps};
        GstElement *encoded = gst_bin_get_by_name(GST_BIN(pipe), "encoded");
        GstElement *jpeg = gst_bin_get_by_name(GST_BIN(pipe), "jpeg");
        g_signal_connect(encoded, "new-sample", G_CALLBACK(encoded_sample), &capture);
        g_signal_connect(jpeg, "new-sample", G_CALLBACK(jpeg_sample), &capture);
        GstBus *bus = gst_element_get_bus(pipe);
        bool failed = gst_element_set_state(pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE;
        auto started = std::chrono::steady_clock::now(), last = started;
        uint64_t last_frames = 0;
        uint64_t last_detections = 0;
        while (!stopping && !failed) {
            GstMessage *message = gst_bus_timed_pop_filtered(bus, GST_SECOND / 5,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (message) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    gchar *debug = nullptr; GError *e = nullptr;
                    gst_message_parse_error(message, &e, &debug);
                    fprintf(stderr, "GStreamer: %s (%s)\n", e->message, debug ? debug : "");
                    g_error_free(e); g_free(debug); failed = true;
                }
                gst_message_unref(message); break;
            }
            auto now = std::chrono::steady_clock::now();
            double seconds = std::chrono::duration<double>(now - started).count();
            auto detection = yolo ? yolo->snapshot() : YoloResult{};
            if (!detection.error.empty()) {
                fprintf(stderr,"DSP inference failed: %s\n",detection.error.c_str()); failed = true; break;
            }
            if (yolo && !detection.frames && seconds > 15) {
                fprintf(stderr,"No DSP results within 15 seconds\n"); failed = true; break;
            }
            if (!capture.encoded && seconds > 15) {
                fprintf(stderr, "No video frames received within 15 seconds\n"); failed = true; break;
            }
            if (o.seconds && seconds >= o.seconds) break;
            double interval = std::chrono::duration<double>(now - last).count();
            if (interval >= 5) {
                auto frames = capture.encoded.load();
                char status[512];
                snprintf(status,sizeof(status),"{\"state\":\"running\",\"yolo\":%s,\"backend\":\"%s\",\"yolo_frames\":%llu,\"yolo_fps\":%.2f,\"inference_ms\":%.2f,\"objects\":%zu,\"video_fps\":%.2f,\"encoding_backend\":\"%s\"}",
                    yolo ? "true" : "false",yolo ? "ELcore50" : "none",
                    static_cast<unsigned long long>(detection.frames),
                    (detection.frames-last_detections)/interval,detection.inference_ms,detection.boxes.size(),(frames-last_frames)/interval,
                    o.software_encoding ? "CPU" : "Mali-V61");
                web.set_status(status);
                printf("video frames=%llu jpeg=%llu fps=%.1f\n",
                    static_cast<unsigned long long>(frames),
                    static_cast<unsigned long long>(capture.jpeg.load()), (frames - last_frames) / interval);
                if (yolo) {
                    printf("YOLO DSP frames=%llu fps=%.1f inference=%.1fms prepare=%.1fms decode=%.1fms total=%.1fms boxes=%zu",
                        static_cast<unsigned long long>(detection.frames),
                        (detection.frames-last_detections)/interval,detection.inference_ms,detection.prepare_ms,detection.decode_ms,detection.total_ms,detection.boxes.size());
                    for (const auto &d : detection.boxes) printf(" %s:%.2f",YoloDSP::label(d.cls),d.score);
                    puts(""); last_detections = detection.frames;
                }
                fflush(stdout); last = now; last_frames = frames;
            }
        }
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(bus); gst_object_unref(encoder); gst_object_unref(encoded);
        gst_object_unref(jpeg); gst_object_unref(pipe);
        printf("Stopped: video=%llu jpeg=%llu\n",
            static_cast<unsigned long long>(capture.encoded.load()),
            static_cast<unsigned long long>(capture.jpeg.load()));
        return failed ? 1 : 0;
    } catch (const std::exception &e) { fprintf(stderr, "inmys-vision: %s\n", e.what()); return 1; }
}
