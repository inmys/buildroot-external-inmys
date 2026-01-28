#include <iostream>
#include <string>
#include <stdexcept>
#include <chrono>
#include <thread>

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// VL53L0X register addresses (from ST API & common libs)
constexpr uint8_t REG_SYSRANGE_START              = 0x00;
constexpr uint8_t REG_SYSTEM_INTERRUPT_CONFIG_GPIO = 0x0A;
constexpr uint8_t REG_SYSTEM_INTERRUPT_CLEAR      = 0x0B;
constexpr uint8_t REG_RESULT_INTERRUPT_STATUS     = 0x13;
constexpr uint8_t REG_RESULT_RANGE_STATUS         = 0x14; // base of result block
constexpr uint8_t REG_GPIO_HV_MUX_ACTIVE_HIGH     = 0x84;
constexpr uint8_t REG_IDENTIFICATION_MODEL_ID     = 0xC0;
constexpr uint8_t EXPECTED_MODEL_ID               = 0xEE; // VL53L0X model ID

// ---------- Low level I2C helpers ----------

int i2c_write_byte(int fd, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    ssize_t n = write(fd, buf, 2);
    return (n == 2) ? 0 : -1;
}

int i2c_read_byte(int fd, uint8_t reg, uint8_t &value)
{
    // Send register address
    ssize_t n = write(fd, &reg, 1);
    if (n != 1) return -1;

    // Read one byte
    n = read(fd, &value, 1);
    if (n != 1) return -1;

    return 0;
}

// Read a 16-bit big-endian value (MSB first) from a register
int i2c_read_word_be(int fd, uint8_t reg, uint16_t &value)
{
    uint8_t buf[2];

    // Send starting register address
    ssize_t n = write(fd, &reg, 1);
    if (n != 1) return -1;

    // Read 2 bytes
    n = read(fd, buf, 2);
    if (n != 2) return -1;

    // MSB is at buf[0], LSB at buf[1]
    value = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    return 0;
}

// ---------- Minimal sensor helper logic ----------

// Optional: very small "init" to configure interrupt behaviour and check model ID
void sensor_basic_init(int fd)
{
    uint8_t model_id = 0;
    if (i2c_read_byte(fd, REG_IDENTIFICATION_MODEL_ID, model_id) == 0)
    {
        if (model_id != EXPECTED_MODEL_ID)
        {
            std::cerr << "Warning: unexpected model ID 0x"
                      << std::hex << static_cast<int>(model_id)
                      << " (expected 0x" << static_cast<int>(EXPECTED_MODEL_ID)
                      << "). Continue anyway.\n" << std::dec;
        }
    }
    else
    {
        std::cerr << "Warning: failed to read model ID register.\n";
    }

    // Configure interrupt as "new sample ready" (same as Pololu init)
    // and make GPIO active low (even if we don't use the pin, the status bits work). :contentReference[oaicite:2]{index=2}
    uint8_t gpio_cfg = 0;
    if (i2c_read_byte(fd, REG_GPIO_HV_MUX_ACTIVE_HIGH, gpio_cfg) == 0)
    {
        gpio_cfg &= ~0x10; // active low
        i2c_write_byte(fd, REG_GPIO_HV_MUX_ACTIVE_HIGH, gpio_cfg);
    }

    i2c_write_byte(fd, REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04); // "new sample ready"
    i2c_write_byte(fd, REG_SYSTEM_INTERRUPT_CLEAR, 0x01);       // clear any pending interrupt
}

// Wait until a new sample is ready (RESULT_INTERRUPT_STATUS bits [2:0] != 0)
// This follows the logic used in the Pololu VL53L0X library. :contentReference[oaicite:3]{index=3}
bool wait_for_new_sample(int fd, int timeout_ms = 1000)
{
    const int step_ms = 5;
    const int max_loops = timeout_ms / step_ms;

    for (int i = 0; i < max_loops; ++i)
    {
        uint8_t status = 0;
        if (i2c_read_byte(fd, REG_RESULT_INTERRUPT_STATUS, status) < 0)
        {
            throw std::runtime_error("Failed to read RESULT_INTERRUPT_STATUS");
        }

        if (status & 0x07) // any non-zero in bits [2:0] means "measurement finished" 
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }

    return false; // timeout
}

// Start a single measurement and read distance in millimeters
uint16_t read_distance_mm(int fd)
{
    // 1. Start a single ranging measurement
    if (i2c_write_byte(fd, REG_SYSRANGE_START, 0x01) < 0)
    {
        throw std::runtime_error("Failed to write SYSRANGE_START");
    }

    // 2. Wait until a new sample is ready
    if (!wait_for_new_sample(fd, 500))
    {
        throw std::runtime_error("Timeout waiting for measurement to complete");
    }

    // 3. Read 16-bit distance from RESULT_RANGE_STATUS + 10 (0x1E)
    //    MSB at 0x1E, LSB at 0x1F (big-endian). 
    uint16_t distance = 0;
    if (i2c_read_word_be(fd, REG_RESULT_RANGE_STATUS + 10, distance) < 0)
    {
        throw std::runtime_error("Failed to read distance (0x1E/0x1F)");
    }

    // 4. Clear the "new sample ready" interrupt
    if (i2c_write_byte(fd, REG_SYSTEM_INTERRUPT_CLEAR, 0x01) < 0)
    {
        throw std::runtime_error("Failed to clear interrupt");
    }

    return distance;
}

// ---------- main ----------

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <i2c_bus> <i2c_addr>\n"
                  << "Example: " << argv[0] << " 1 0x29\n";
        return 1;
    }

    try
    {
        // I2C bus number, e.g. 1 -> /dev/i2c-1
        int bus = std::stoi(argv[1]);
        if (bus < 0)
        {
            throw std::runtime_error("I2C bus number must be non-negative");
        }

        // Device address: can be passed as 0x29 or 41
        int addr_int = std::stoi(argv[2], nullptr, 0); // base 0 => auto (0x.., 0.., decimal)
        if (addr_int < 0 || addr_int > 0x7F)
        {
            throw std::runtime_error("Invalid 7-bit I2C address");
        }
        uint8_t addr = static_cast<uint8_t>(addr_int);

        std::string dev = "/dev/i2c-" + std::to_string(bus);

        int fd = open(dev.c_str(), O_RDWR);
        if (fd < 0)
        {
            perror("Failed to open I2C device");
            return 1;
        }

        if (ioctl(fd, I2C_SLAVE, addr) < 0)
        {
            perror("Failed to set I2C_SLAVE");
            close(fd);
            return 1;
        }

        std::cout << "Connected to " << dev << ", address 0x"
                  << std::hex << static_cast<int>(addr) << std::dec << "\n";

        // Give the sensor some time after power-up
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Optional: do a minimal sensor configuration
        sensor_basic_init(fd);

        // Continuous loop: read distance every second
        while (true)
        {
            try
            {
                uint16_t distance = read_distance_mm(fd);
                std::cout << "Distance: " << distance << " mm" << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Measurement error: " << e.what() << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        close(fd);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
