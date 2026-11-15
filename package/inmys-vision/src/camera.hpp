// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
struct CameraSelection {
    std::string sensor, setup;
    unsigned mode;
};
CameraSelection select_camera(const std::string &sensor, const std::string &setup,
                              int mode, const std::string &profile = "/run/inmys-camera.conf");
