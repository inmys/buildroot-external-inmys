// SPDX-License-Identifier: Apache-2.0
#include "rtsp.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
Rtsp::Rtsp(int port, unsigned fps) : frame_duration(fps ? GST_SECOND / fps : 0) {
    if (!fps) throw std::invalid_argument("RTSP frame rate must be positive");
    gst_init(nullptr, nullptr);
    // Detect absent runtime plugins before starting the camera.
    for (const char *name : {"appsrc", "h264parse", "rtph264pay"}) {
        auto *factory = gst_element_factory_find(name);
        if (!factory) throw std::runtime_error(std::string("Missing GStreamer plugin: ") + name);
        gst_object_unref(factory);
    }
    loop = g_main_loop_new(nullptr, FALSE);
    server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, std::to_string(port).c_str());
    auto *mounts = gst_rtsp_server_get_mount_points(server);
    factory = gst_rtsp_media_factory_new();
    gst_object_ref(factory); // Keep one reference after mount points take ownership.
    std::string launch =
        "( appsrc name=source is-live=true format=time block=false max-bytes=524288 "
        "caps=\"video/x-h264,stream-format=(string)byte-stream,alignment=(string)au,framerate=(fraction)" +
        std::to_string(fps) + "/1\" ! h264parse ! rtph264pay name=pay0 pt=96 config-interval=-1 )";
    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_latency(factory, 0);
    g_signal_connect(factory, "media-configure", G_CALLBACK(configured), this);
    gst_rtsp_mount_points_add_factory(mounts, "/live", factory);
    g_object_unref(mounts);
    source = gst_rtsp_server_attach(server, nullptr);
    if (!source) { g_object_unref(server); g_main_loop_unref(loop); throw std::runtime_error("RTSP listen failed"); }
    thread = std::thread([this] { g_main_loop_run(loop); });
    printf("H.264 RTSP: rtsp://<module-ip>:%d/live\n", port);
}
void Rtsp::configured(GstRTSPMediaFactory *, GstRTSPMedia *media, gpointer data) {
    auto &self = *static_cast<Rtsp *>(data);
    auto *element = gst_rtsp_media_get_element(media);
    auto *src = gst_bin_get_by_name_recurse_up(GST_BIN(element), "source");
    gst_object_unref(element);
    if (!src) return;
    std::lock_guard<std::mutex> lock(self.mutex);
    if (self.shutting_down) { gst_object_unref(src); return; }
    if (self.current_media) {
        g_signal_handlers_disconnect_by_data(self.current_media, &self);
        gst_object_unref(self.current_media);
    }
    if (self.appsrc) gst_object_unref(self.appsrc);
    self.appsrc = GST_APP_SRC(src); self.current_media = GST_RTSP_MEDIA(gst_object_ref(media));
    self.awaiting_key = true; self.origin = 0; self.have_origin = false; self.idr = true;
    // Limit prepared-media lifetime to the connected clients.
    g_signal_connect(media, "unprepared", G_CALLBACK(unprepared), &self);
    printf("RTSP media configured; requesting IDR\n"); fflush(stdout);
}
void Rtsp::unprepared(GstRTSPMedia *media, gpointer data) {
    auto &self = *static_cast<Rtsp *>(data);
    std::lock_guard<std::mutex> lock(self.mutex);
    if (media == self.current_media) {
        if (self.appsrc) gst_object_unref(self.appsrc);
        self.appsrc = nullptr; self.current_media = nullptr;
        g_signal_handlers_disconnect_by_data(media, &self);
        gst_object_unref(media);
        printf("RTSP media unprepared\n"); fflush(stdout);
    }
}
void Rtsp::push(const std::vector<unsigned char> &packet, bool key, uint64_t nanoseconds) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!appsrc || packet.empty()) return;
    // Never stall capture/display for a slow RTSP client. After overflow,
    // resume at an IDR rather than send undecodable dependent P frames.
    if (gst_app_src_get_current_level_bytes(appsrc) >= 524288) {
        if (!awaiting_key) { idr = true; fprintf(stderr, "RTSP queue full; waiting for IDR\n"); }
        awaiting_key = true; return;
    }
    if (awaiting_key && !key) return;
    if (!have_origin) { origin = nanoseconds; have_origin = true; }
    auto *buffer = gst_buffer_new_allocate(nullptr, packet.size(), nullptr);
    gst_buffer_fill(buffer, 0, packet.data(), packet.size());
    GST_BUFFER_PTS(buffer) = GST_BUFFER_DTS(buffer) = nanoseconds - origin;
    GST_BUFFER_DURATION(buffer) = frame_duration;
    if (!key) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    if (awaiting_key) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
    awaiting_key = false;
    auto ret = gst_app_src_push_buffer(appsrc, buffer); // Takes ownership.
    if (ret != GST_FLOW_OK) { awaiting_key = true; idr = true; }
}
static GstRTSPFilterResult remove_client(GstRTSPServer *, GstRTSPClient *client, gpointer) {
    gst_rtsp_client_close(client); return GST_RTSP_FILTER_REMOVE;
}
static GstRTSPFilterResult remove_session(GstRTSPSessionPool *, GstRTSPSession *, gpointer) {
    return GST_RTSP_FILTER_REMOVE;
}
Rtsp::~Rtsp() {
    g_main_loop_quit(loop); if (thread.joinable()) thread.join();
    g_signal_handlers_disconnect_by_data(factory, this);
    GstRTSPMedia *media;
    { std::lock_guard<std::mutex> lock(mutex);
      shutting_down = true; media = current_media; current_media = nullptr;
      if (media) g_signal_handlers_disconnect_by_data(media, this);
      if (appsrc) gst_object_unref(appsrc);
      appsrc = nullptr; }
    // Unprepare before releasing sessions, and join library worker threads.
    if (media) { gst_rtsp_media_unprepare(media); gst_object_unref(media); }
    gst_rtsp_server_client_filter(server, remove_client, nullptr);
    auto *pool = gst_rtsp_server_get_session_pool(server);
    gst_rtsp_session_pool_filter(pool, remove_session, nullptr); g_object_unref(pool);
    if (source) g_source_remove(source);
    gst_object_unref(factory); g_object_unref(server); g_main_loop_unref(loop);
    gst_rtsp_thread_pool_cleanup();
}
