// SPDX-License-Identifier: Apache-2.0
#include "yolo.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
namespace {
void pixel(GstVideoFrame &f, int x, int y, unsigned char value) {
    int w = GST_VIDEO_FRAME_WIDTH(&f), h = GST_VIDEO_FRAME_HEIGHT(&f);
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    auto *luma = static_cast<unsigned char *>(GST_VIDEO_FRAME_PLANE_DATA(&f,0));
    auto *uv = static_cast<unsigned char *>(GST_VIDEO_FRAME_PLANE_DATA(&f,1));
    luma[y*GST_VIDEO_FRAME_PLANE_STRIDE(&f,0)+x] = value;
    auto offset = (y/2)*GST_VIDEO_FRAME_PLANE_STRIDE(&f,1)+(x & ~1);
    uv[offset] = uv[offset+1] = 128;
}
void text(GstVideoFrame &f, int x, int y, const char *label) {
    static const unsigned char font[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,1},
        {0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,2,0x0c,2,0x7f},
        {0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},{0x7f,9,9,9,6},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
        {1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
        {0,0,0x60,0x60,0},{0x23,0x13,8,0x64,0x62},{0,0x36,0x36,0,0}
    };

    int width = GST_VIDEO_FRAME_WIDTH(&f);
    for (; *label && x+12 < width; ++label,x+=12) {
        int c = std::toupper(static_cast<unsigned char>(*label)), index = -1;
        if (c >= '0' && c <= '9') index = c-'0';
        if (c >= 'A' && c <= 'Z') index = c-'A'+10;
        if (c == '.') index = 36;
        if (c == '%') index = 37;
        if (c == ':') index = 38;
        for (int xx=0; xx<12; ++xx) for (int yy=0; yy<16; ++yy) {
            bool on = index >= 0 && xx < 10 && yy < 14 && (font[index][xx/2] & (1 << (yy/2)));
            pixel(f,x+xx,y+yy,on ? 235 : 16);
        }
    }
}
}
void draw_detections(GstVideoFrame &f, const YoloResult &result) {
    char label[128];
    snprintf(label,sizeof(label),"YOLO DSP: %.0f MS",result.inference_ms);
    text(f,8,8,label);
    // Drop stale boxes after a stalled input or slow inference; avoid old labels forever.
    if (std::chrono::steady_clock::now()-result.captured > std::chrono::seconds(1)) return;
    for (const auto &d : result.boxes) {
        int x1 = d.x1, y1 = d.y1, x2 = d.x2, y2 = d.y2;
        for (int t=-1; t<=2; ++t) {
            unsigned char color = (t == -1 || t == 2) ? 16 : 235;
            for (int x=x1; x<=x2; ++x) { pixel(f,x,y1+t,color); pixel(f,x,y2+t,color); }
            for (int y=y1; y<=y2; ++y) { pixel(f,x1+t,y,color); pixel(f,x2+t,y,color); }
        }
        snprintf(label,sizeof(label),"%s %.0f%%",YoloDSP::label(d.cls),d.score*100);
        text(f,x1,std::max(28,y1-18),label);
    }
}
