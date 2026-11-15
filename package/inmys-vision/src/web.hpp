// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
class Web {
    int listener = -1;
    std::atomic<bool> stopping{false};
    std::thread thread;
    std::mutex mutex;
    std::vector<unsigned char> latest;
    uint64_t sequence = 0;
    std::string html;
    std::string status = "{\"state\":\"starting\"}";
    void serve();
public:
    explicit Web(int port = 8081, int rtsp_port = 8554);
    ~Web();
    void push(std::vector<unsigned char> jpeg);
    void set_status(std::string json);
};
