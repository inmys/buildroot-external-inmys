// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>
class Rtsp {
    GstRTSPServer *server = nullptr;
    GstRTSPMediaFactory *factory = nullptr;
    GMainLoop *loop = nullptr;
    guint source = 0;
    std::thread thread;
    std::mutex mutex;
    GstAppSrc *appsrc = nullptr;
    GstRTSPMedia *current_media = nullptr; // Own reference until unprepared.
    uint64_t origin = 0;
    bool awaiting_key = true;
    bool have_origin = false;
    bool shutting_down = false;
    uint64_t frame_duration;
    std::atomic<bool> idr{false};
    static void configured(GstRTSPMediaFactory *, GstRTSPMedia *, gpointer);
    static void unprepared(GstRTSPMedia *, gpointer);
public:
    explicit Rtsp(int port = 8554, unsigned fps = 15);
    ~Rtsp();
    bool need_idr() { return idr.exchange(false); }
    void push(const std::vector<unsigned char> &packet, bool key, uint64_t nanoseconds);
};
