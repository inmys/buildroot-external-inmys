// SPDX-License-Identifier: Apache-2.0
#include "yolo.hpp"
#include <elcorenn/elcorenn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {
constexpr unsigned side = 640, plane = side * side, cells = 8400;
using Clock = std::chrono::steady_clock;
double ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }
void shape(uint32_t model, bool input, std::initializer_list<uint32_t> expected) {
    uint32_t dimensions[MAX_NDIM_VALUE + 1]{};
    if (input) GetInputShape(model, 0, dimensions); else GetOutputShape(model, 0, dimensions);
    if (dimensions[0] != expected.size() || !std::equal(expected.begin(), expected.end(), dimensions + 1))
        throw std::runtime_error("unsupported YOLO model tensor shape");
}
float overlap(const Detection &a, const Detection &b) {
    float intersection = std::max(0.f, std::min(a.x2,b.x2)-std::max(a.x1,b.x1)) *
                         std::max(0.f, std::min(a.y2,b.y2)-std::max(a.y1,b.y1));
    float area = (a.x2-a.x1)*(a.y2-a.y1) + (b.x2-b.x1)*(b.y2-b.y1) - intersection;
    return area > 0 ? intersection / area : 0;
}

}

YoloDSP::YoloDSP(const std::string &directory, const std::string &statistics_path) : statistics(statistics_path) {
    if (!std::ifstream(directory+"/model.json") || !std::ifstream(directory+"/model.bin"))
        throw std::runtime_error("YOLO model files missing: " + directory);
    try {
        uint32_t device = 0;
        backend = InitBackend(1, &device, ENNHeapSize::Size_512MB); backend_open = true;
        model = LoadModel((directory+"/model.json").c_str(), (directory+"/model.bin").c_str(),
                          ENNDataType::FLOAT16, backend); model_open = true;
        if (GetInputsNumber(model) != 1 || GetOutputsNumber(model) != 1)
            throw std::runtime_error("expected one YOLO input and one output");
        shape(model, true, {1,3,side,side}); shape(model, false, {1,84,cells});
        input.resize(3*plane); prepared_input.resize(3*plane); queued_input.resize(3*plane);
        output.resize(84*cells);
        printf("YOLOv8n FP16: ELcore50 device=0 heap=512MiB, input=RGB NCHW 640x640, no CPU backend\n");
        worker = std::thread([this] { work(); });
    } catch (...) { release(); throw; }
}
void YoloDSP::release() {
    if (model_open) { ReleaseModel(model); model_open = false; }
    if (backend_open) { ReleaseBackend(backend); backend_open = false; }
}
YoloDSP::~YoloDSP() {
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; ready.notify_one(); }
    if (worker.joinable()) worker.join();
    release();
}
void YoloDSP::submit(const GstVideoFrame &frame) {
    { std::lock_guard<std::mutex> lock(mutex);
      if (stopping || pending || !result.error.empty()) return; }
    // Read the camera lease before drawing; only the resized model tensor crosses
    // to the DSP worker. Never copy or retain a full-resolution raw frame.
    auto captured_now = Clock::now();
    prepare(frame, prepared_input);
    auto elapsed = ms(Clock::now() - captured_now);
    std::lock_guard<std::mutex> lock(mutex);
    prepared_input.swap(queued_input);
    queued_info = frame.info; queued_prepare_ms = elapsed;
    queued_captured = captured_now; pending = true; ready.notify_one();
}
YoloResult YoloDSP::snapshot() const { std::lock_guard<std::mutex> lock(mutex); return result; }
void YoloDSP::prepare(const GstVideoFrame &frame, std::vector<float> &tensor) {
    const auto &info = frame.info;
    int w = GST_VIDEO_FRAME_WIDTH(&frame), h = GST_VIDEO_FRAME_HEIGHT(&frame);
    float scale = static_cast<float>(side)/std::max(w,h);
    int sw = std::lround(w*scale), sh = std::lround(h*scale);
    std::fill(tensor.begin(), tensor.end(), 0.f);
    // Resize NV12 before RGB conversion. Converting full-resolution RGB first
    // is expensive on A53. Precompute bilinear coordinates outside the pixel loop.
    struct Axis { int lo, hi, fraction; };
    auto axis = [](float position, int length) {
        position = std::clamp(position,0.f,static_cast<float>(length-1));
        int lo = position;
        return Axis{lo,std::min(lo+1,length-1),static_cast<int>((position-lo)*256)};
    };
    std::vector<Axis> lx(sw), cx(sw), ly(sh), cy(sh);
    for (int x=0; x<sw; ++x) {
        float sx = (x+.5f)*w/sw-.5f;
        lx[x] = axis(sx,w); cx[x] = axis((sx-.5f)/2,w/2);
    }
    for (int y=0; y<sh; ++y) {
        float sy = (y+.5f)*h/sh-.5f;
        ly[y] = axis(sy,h); cy[y] = axis((sy-.5f)/2,h/2);
    }
    auto interpolate = [](const unsigned char *data, const Axis &x, const Axis &y, int step, int stride) {
        int top = data[y.lo*stride+x.lo*step]*(256-x.fraction)+data[y.lo*stride+x.hi*step]*x.fraction;
        int bottom = data[y.hi*stride+x.lo*step]*(256-x.fraction)+data[y.hi*stride+x.hi*step]*x.fraction;
        return (top*(256-y.fraction)+bottom*y.fraction+32768)>>16;
    };
    bool full = info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
    bool bt709 = info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT709;
    float kr = bt709 ? .2126f : .299f, kb = bt709 ? .0722f : .114f;
    float chroma = full ? 256.f : 256.f*255/224;
    int ys = full ? 256 : 298;
    int rv = std::lround(2*(1-kr)*chroma), bu = std::lround(2*(1-kb)*chroma);
    int gu = std::lround(2*kb*(1-kb)/(1-kr-kb)*chroma);
    int gv = std::lround(2*kr*(1-kr)/(1-kr-kb)*chroma);
    const auto *luma = static_cast<const unsigned char *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    const auto *uv = static_cast<const unsigned char *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 1));
    int ys_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1);
    for (int y=0; y<sh; ++y) for (int x=0; x<sw; ++x) {
        int yy = (interpolate(luma,lx[x],ly[y],1,ys_stride)-(full ? 0 : 16))*ys;
        int u = interpolate(uv,cx[x],cy[y],2,uv_stride)-128, v = interpolate(uv+1,cx[x],cy[y],2,uv_stride)-128;
        unsigned offset = y*side+x;
        tensor[offset] = std::clamp((yy+rv*v+128)>>8,0,255)/255.f;
        tensor[plane+offset] = std::clamp((yy-gu*u-gv*v+128)>>8,0,255)/255.f;
        tensor[2*plane+offset] = std::clamp((yy+bu*u+128)>>8,0,255)/255.f;
    }
}
std::vector<Detection> YoloDSP::decode() const {
    std::vector<Detection> candidates, boxes;
    // Scan each contiguous channel once. Cell-first traversal thrashes A53 caches.
    std::vector<float> scores(cells,.30f);
    std::vector<int> classes(cells,-1);
    for (unsigned c = 0; c < 80; ++c) for (unsigned n = 0; n < cells; ++n) {
        float s = output[(4+c)*cells+n];
        if (s >= scores[n] && s <= 1) { scores[n] = s; classes[n] = c; }
    }
    for (unsigned n = 0; n < cells; ++n) {
        float score = scores[n]; int cls = classes[n];
        if (cls < 0) continue;
        float x = output[n], y = output[cells+n], w = output[2*cells+n], h = output[3*cells+n];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) continue;
        candidates.push_back({x-w/2,y-h/2,x+w/2,y+h/2,score,cls});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
    for (auto d : candidates) {
        bool suppressed = false;
        for (const auto &b : boxes) if (b.cls == d.cls && overlap(b,d) > .45f) { suppressed = true; break; }
        if (!suppressed) { boxes.push_back(d); if (boxes.size() == 100) break; }
    }
    int w = GST_VIDEO_INFO_WIDTH(&info), h = GST_VIDEO_INFO_HEIGHT(&info);
    float scale = static_cast<float>(std::max(w,h))/side;
    // Vendor model uses padding on bottom/right, with no left/top offset.
    for (auto &d : boxes) {
        d.x1 = std::clamp(d.x1*scale,0.f,static_cast<float>(w-1));
        d.x2 = std::clamp(d.x2*scale,0.f,static_cast<float>(w-1));
        d.y1 = std::clamp(d.y1*scale,0.f,static_cast<float>(h-1));
        d.y2 = std::clamp(d.y2*scale,0.f,static_cast<float>(h-1));
    }
    boxes.erase(std::remove_if(boxes.begin(),boxes.end(),[](const auto &d) { return d.x2 <= d.x1 || d.y2 <= d.y1; }),boxes.end());
    return boxes;
}
void YoloDSP::work() {
    for (;;) {
        double prepare_ms;
        { std::unique_lock<std::mutex> lock(mutex); ready.wait(lock,[this] { return pending || stopping; });
          if (stopping) break;
          input.swap(queued_input); info = queued_info; captured = queued_captured;
          prepare_ms = queued_prepare_ms;
          pending = false; }
        try {
            auto start = Clock::now();
            float *in = input.data(), *out = output.data();
            auto began = Clock::now(); InvokeModel(model,&in,&out,1); auto ended = Clock::now();
            auto boxes = decode();
            std::lock_guard<std::mutex> lock(mutex);
            result.boxes = std::move(boxes); ++result.frames; result.captured = captured;
            result.inference_ms = ms(ended-began); result.total_ms = prepare_ms + ms(Clock::now()-start);
            result.prepare_ms = prepare_ms; result.decode_ms = ms(Clock::now()-ended);
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(mutex); result.error = e.what(); break;
        }
    }
    if (!statistics.empty()) try { SaveModelStatisticToCSV(model,statistics.c_str()); }
    catch (const std::exception &e) { fprintf(stderr,"DSP statistics: %s\n",e.what()); }
}

const char *YoloDSP::label(int cls) {
    static const char *labels[] = {"person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant","bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"};
    return cls >= 0 && cls < 80 ? labels[cls] : "unknown";
}
