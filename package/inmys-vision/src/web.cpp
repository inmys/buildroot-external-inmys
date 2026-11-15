// SPDX-License-Identifier: Apache-2.0
#include "web.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
static double now() { timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec+t.tv_nsec/1e9; }
static const char page[] = R"HTML(<!doctype html>
<html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>inmys — видео с камеры</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#101719;color:#edf4f3;font:16px system-ui,sans-serif}main{max-width:960px;margin:auto;padding:20px}header{display:flex;justify-content:space-between;align-items:center;margin:6px 0 28px}.brand{font-size:26px;font-weight:750;letter-spacing:-1px}.live{font-size:13px;color:#80eac5}.live:before{content:'●';margin-right:8px}h1{font-size:24px;margin:0 0 8px}p{color:#aabbb7;line-height:1.5;margin:0 0 18px}.video{background:#000;border-radius:12px;overflow:hidden;border:1px solid #344440}img{display:block;width:100%;aspect-ratio:16/9;object-fit:contain}button{margin-top:16px;border:1px solid #40534e;background:#1a2924;color:#e2f8ee;padding:12px 18px;border-radius:8px;font:inherit;cursor:pointer}.foot{font-size:13px;margin-top:20px}a{color:#80eac5}
</style><main><header><span class="brand">inmys</span><span class="live">Прямой эфир</span></header>
<h1>Видео с камеры</h1><p id="status">Подключение к видеопотоку…</p>
<div class="video" id="viewer"><img id="camera" src="/video.mjpg" alt="Живое видео с камеры"></div>
<button onclick="document.getElementById('viewer').requestFullscreen?.()">На весь экран</button>
<p class="foot">Для просмотра в VLC: <a id="rtsp">RTSP</a></p></main>
<script>const rtsp=document.getElementById('rtsp');rtsp.href='rtsp://'+location.hostname+':8554/live';rtsp.textContent=rtsp.href;const camera=document.getElementById('camera');camera.onerror=()=>setTimeout(()=>{camera.src='/video.mjpg?t='+Date.now()},1500);async function status(){try{const s=await(await fetch('/status.json')).json();document.getElementById('status').textContent=s.yolo?'YOLOv8n · DSP · '+s.yolo_fps.toFixed(1)+' кадров/с · объектов: '+s.objects:(s.state==='running'?'Прямой эфир с камеры':'Запуск…')}catch(e){document.getElementById('status').textContent='Нет связи с модулем'}setTimeout(status,2000)}status();</script></html>)HTML";
struct Client {
    int fd; bool reading = true, stream = false, closed = false;
    std::string request;
    std::vector<unsigned char> pending;
    size_t offset = 0; uint64_t sequence = 0; double deadline;
};
static void queue(Client &c, const std::string &bytes) {
    c.pending.assign(bytes.begin(), bytes.end()); c.offset = 0; c.deadline = now()+3;
}
Web::Web(int port, int rtsp_port) : html(page) {
    auto at = html.find(":8554/live");
    if (at != std::string::npos) html.replace(at, 10, ":" + std::to_string(rtsp_port) + "/live");
    listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listener < 0) throw std::runtime_error("HTTP socket failed");
    int on = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port); address.sin_addr.s_addr = INADDR_ANY;
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(listener, 8)) {
        close(listener); throw std::runtime_error("HTTP listen failed");
    }
    thread = std::thread([this] { serve(); });
    printf("Browser preview: http://<module-ip>:%d/ (MJPEG)\n", port);
}
void Web::push(std::vector<unsigned char> jpeg) {
    std::lock_guard<std::mutex> lock(mutex);
    latest = std::move(jpeg); ++sequence;
}
void Web::set_status(std::string json) { std::lock_guard<std::mutex> lock(mutex); status = std::move(json); }
Web::~Web() { stopping = true; if (thread.joinable()) thread.join(); close(listener); }
void Web::serve() {
    std::vector<Client> clients;
    while (!stopping) {
        // Only the latest JPEG is retained. Slow clients never block the video pipeline.
        { std::lock_guard<std::mutex> lock(mutex);
          for (auto &c : clients) if (c.stream && c.pending.empty() && c.sequence != sequence && !latest.empty()) {
            queue(c, "--inmysframe\r\nContent-Type: image/jpeg\r\nContent-Length: "+std::to_string(latest.size())+"\r\n\r\n");
            c.pending.insert(c.pending.end(), latest.begin(), latest.end());
            c.pending.push_back('\r'); c.pending.push_back('\n'); c.sequence = sequence;
          } }
        std::vector<pollfd> events{{listener,POLLIN,0}};
        for (const auto &c : clients) events.push_back({c.fd, static_cast<short>(POLLIN | (c.pending.empty()?0:POLLOUT)), 0});
        int ready = poll(events.data(), events.size(), 50);
        if (ready < 0) { if (errno == EINTR) continue; break; }
        // Process existing clients first; poll indexes stay stable until this loop ends.
        for (size_t i = 0; i < clients.size(); ++i) {
            auto &c = clients[i]; auto &event = events[i+1];
            if (event.revents & (POLLERR | POLLHUP | POLLNVAL)) c.closed = true;
            if (!c.closed && (event.revents & POLLIN)) {
                char data[2048]; ssize_t length = recv(c.fd, data, sizeof(data), MSG_DONTWAIT);
                if (!length || (length < 0 && errno != EAGAIN && errno != EINTR)) c.closed = true;
                if (length > 0 && c.reading) {
                    c.request.append(data, length);
                    if (c.request.size() > 4096) c.closed = true;
                    if (c.request.find("\r\n\r\n") != std::string::npos) {
                        c.reading = false;
                        size_t end = c.request.find(" HTTP/");
                        std::string path = end == std::string::npos ? "" : c.request.substr(4,end-4);
                        if (c.request.compare(0,4,"GET ")) path.clear();
                        if (path == "/" || path == "/index.html") {
                            queue(c,"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(html.size())+"\r\n\r\n"+html);
                        } else if (path == "/status.json") {
                            std::lock_guard<std::mutex> lock(mutex);
                            queue(c,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(status.size())+"\r\n\r\n"+status);
                        } else if (path == "/video.mjpg" || path.compare(0,12,"/video.mjpg?") == 0) {
                            c.stream = true;
                            queue(c,"HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=inmysframe\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
                        } else queue(c,"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        c.request.clear();
                    }
                }
            }
            if (!c.closed && (event.revents & POLLOUT) && !c.pending.empty()) {
                ssize_t sent = send(c.fd,c.pending.data()+c.offset,c.pending.size()-c.offset,MSG_NOSIGNAL|MSG_DONTWAIT);
                if (sent > 0) c.offset += sent;
                else if (sent < 0 && errno != EAGAIN && errno != EINTR) c.closed = true;
                if (c.offset == c.pending.size()) {
                    c.pending.clear(); c.offset = 0;
                    if (!c.stream) c.closed = true;
                }
            }
            if ((c.reading || !c.pending.empty()) && now() > c.deadline) c.closed = true;
        }
        for (auto &c : clients) if (c.closed) close(c.fd);
        clients.erase(std::remove_if(clients.begin(),clients.end(),[](const auto &c) { return c.closed; }),clients.end());
        if (events[0].revents & POLLIN) for (;;) {
            int fd = accept4(listener,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
            if (fd < 0) break;
            if (clients.size() >= 8) { close(fd); continue; }
            Client c{}; c.fd = fd; c.deadline = now()+3; clients.push_back(std::move(c));
        }
    }
    for (auto &c : clients) close(c.fd);
}
