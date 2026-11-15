// SPDX-License-Identifier: Apache-2.0
#include "camera.hpp"
#include <gpiod.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
struct Preset {
    const char *name, *setup;
    unsigned mode;
    unsigned short address, id_register, id;
};
constexpr std::array<Preset,3> presets{{
    {"IMX219", "/etc/felix/imx219/imx219.cfg", 2, 0x10, 0x0000, 0x0219},
    {"OV5647", "/etc/felix/ov5647/ov5647.cfg", 0, 0x36, 0x300a, 0x5647},
    {"IMX477", "/etc/felix/imx477/imx477.cfg", 6, 0x1a, 0x0016, 0x0477},
}};
struct Profile {
    int csi, bus, power_chip, power_line, power_on, reset_chip, reset_line;
};
Profile load_profile(const std::string &path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Camera wiring profile missing: " + path + "; run S12nmscsi");
    std::map<std::string,int> fields;
    std::string key; int value;
    while (file >> key >> value) fields[key] = value;
    if (!file.eof()) throw std::runtime_error("Malformed camera wiring profile: " + path);
    for (const char *name : {"csi","i2c","power_chip","power_line","power_on","reset_chip","reset_line"})
        if (!fields.count(name)) throw std::runtime_error(std::string("Camera profile lacks ") + name);
    Profile p{fields.at("csi"),fields.at("i2c"),fields.at("power_chip"),fields.at("power_line"),
              fields.at("power_on"),fields.at("reset_chip"),fields.at("reset_line")};
    if (p.csi < 0 || p.csi > 1 || p.bus < 0 || p.bus > 255 ||
        p.power_chip < 0 || p.power_line < 0 || p.reset_chip < 0 || p.reset_line < -1 ||
        (p.power_on != 0 && p.power_on != 1))
        throw std::runtime_error("Invalid camera wiring profile: " + path);
    return p;
}
std::runtime_error system_error(const std::string &what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}
class Output {
    std::unique_ptr<gpiod_chip,decltype(&gpiod_chip_close)> chip{nullptr,gpiod_chip_close};
    gpiod_line *line = nullptr;
public:
    Output(int controller, int offset, int initial) {
        chip.reset(gpiod_chip_open_by_number(static_cast<unsigned>(controller)));
        if (!chip) throw system_error("Open camera GPIO chip");
        line = gpiod_chip_get_line(chip.get(),static_cast<unsigned>(offset));
        if (!line || gpiod_line_request_output(line,"inmys-camera-probe",initial))
            throw system_error("Request camera GPIO (stop any active camera service first)");
    }
    void set(int value) {
        if (gpiod_line_set_value(line,value)) throw system_error("Set camera GPIO");
    }
};
class Bus {
    int fd;
public:
    explicit Bus(int bus) : fd(open(("/dev/i2c-"+std::to_string(bus)).c_str(),O_RDWR|O_CLOEXEC)) {
        if (fd < 0) throw system_error("Open camera I2C bus");
    }
    ~Bus() { close(fd); }
    bool matches(const Preset &preset) {
        unsigned char reg[2]{static_cast<unsigned char>(preset.id_register >> 8),
                             static_cast<unsigned char>(preset.id_register)};
        unsigned char data[2]{};
        i2c_msg messages[2]{{preset.address,0,2,reg},{preset.address,I2C_M_RD,2,data}};
        i2c_rdwr_ioctl_data transfer{messages,2};
        int ret;
        do { ret = ioctl(fd,I2C_RDWR,&transfer); } while (ret < 0 && errno == EINTR);
        if (ret < 0) {
            if (errno == ENXIO || errno == EREMOTEIO || errno == EIO || errno == ENODEV) return false;
            throw system_error("Read camera chip ID");
        }
        return ret == 2 && ((static_cast<unsigned>(data[0]) << 8) | data[1]) == preset.id;
    }
};
const Preset &detect(const Profile &profile) {
    // Own GPIOs only while probing; Felix requests them after we return.
    // The supplied carrier profiles use a high enable and active-low reset.
    // Expanders retain output levels across a warm SoC reset. Force a real
    // shutdown/enable transition, especially on EVM which has no reset wire.
    Output power(profile.power_chip,profile.power_line,1-profile.power_on);
    std::unique_ptr<Output> reset;
    if (profile.reset_line >= 0)
        reset = std::make_unique<Output>(profile.reset_chip,profile.reset_line,0);
    usleep(10000);
    power.set(profile.power_on);
    usleep(10000);
    if (reset) reset->set(1);
    usleep(100000);
    Bus bus(profile.bus);
    const Preset *found = nullptr;
    // Read only the documented chip-ID registers, never scan/write arbitrary devices.
    for (const auto &preset : presets) if (bus.matches(preset)) {
        if (found) throw std::runtime_error("More than one supported camera responded on this connector");
        found = &preset;
    }
    if (!found) throw std::runtime_error("No supported IMX219/OV5647/IMX477 chip ID on I2C bus " +
                                       std::to_string(profile.bus));
    printf("Camera detected: %s, I2C%d address 0x%02x, CSI%d\n",
           found->name,profile.bus,found->address,profile.csi); fflush(stdout);
    return *found;
}
}
CameraSelection select_camera(const std::string &requested, const std::string &setup,
                              int mode, const std::string &path) {
    std::string name = requested;
    for (char &ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const Profile profile = load_profile(path);
    const Preset *preset = nullptr;
    if (name == "AUTO") preset = &detect(profile);
    else {
        for (const auto &candidate : presets) {
            std::string base = candidate.name;
            if (name == base || name == base + "_" + std::to_string(profile.csi)) preset = &candidate;
            else if (name == base + "_0" || name == base + "_1")
                throw std::runtime_error("Sensor suffix does not match wired CSI" + std::to_string(profile.csi));
        }
    }
    if (!preset) {
        if (mode < 0 || setup.empty())
            throw std::runtime_error("Unknown sensor preset; supply both --mode and --setup");
        return {requested,setup,static_cast<unsigned>(mode)};
    }
    return {std::string(preset->name)+"_"+std::to_string(profile.csi),
            setup.empty() ? preset->setup : setup,
            mode < 0 ? preset->mode : static_cast<unsigned>(mode)};
}
