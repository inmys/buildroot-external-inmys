// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <gst/video/video.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct Detection { float x1, y1, x2, y2, score; int cls; };
struct YoloResult {
    std::vector<Detection> boxes;
    uint64_t frames = 0;
    double inference_ms = 0, total_ms = 0, prepare_ms = 0, decode_ms = 0;
    std::chrono::steady_clock::time_point captured{};
    std::string error;
};

// One explicitly selected ELcore50. A53 handles preparation, NMS and drawing.
// No libcpunn linkage and no CPU inference fallback.
class YoloDSP {
    uint32_t backend = 0, model = 0;
    bool backend_open = false, model_open = false;
    std::vector<float> input, output;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable ready;
    bool stopping = false, pending = false;
    std::vector<unsigned char> pixels, queued_pixels;
    GstVideoInfo info{}, queued_info{};
    std::chrono::steady_clock::time_point captured, queued_captured;
    YoloResult result;
    std::string statistics;
    void work();
    void prepare();
    std::vector<Detection> decode() const;
    void release();
public:
    YoloDSP(const std::string &directory, const std::string &statistics_path);
    ~YoloDSP();
    YoloDSP(const YoloDSP &) = delete;
    YoloDSP &operator=(const YoloDSP &) = delete;
    void submit(const GstVideoFrame &frame);
    YoloResult snapshot() const;
    static const char *label(int cls);
};

void draw_detections(GstVideoFrame &frame, const YoloResult &result);
