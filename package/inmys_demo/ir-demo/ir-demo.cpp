#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/lirc.h>
#include <cerrno>
#include <cstring>

int main(int argc, char* argv[])
{
    // Use device from command line if provided, otherwise default to /dev/lirc0
    const char* dev = (argc > 1) ? argv[1] : "/dev/lirc0";

    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [device]\n"
                  << "Example: " << argv[0] << " /dev/lircd\n";
        return 1;
    }

    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open " << dev << ": "
                  << std::strerror(errno) << std::endl;
        return 1;
    }

    // Try to set the receiver to LIRC_MODE2 (raw pulses/spaces)
    __u32 mode = LIRC_MODE_MODE2;
    if (ioctl(fd, LIRC_SET_REC_MODE, &mode) == -1) {
        std::cerr << "ioctl(LIRC_SET_REC_MODE) failed: "
                  << std::strerror(errno) << std::endl;
        // Many drivers are already in MODE2 by default, so we can continue
    }

    std::cout << "Reading IR events from " << dev << " ..." << std::endl;

    while (true) {
        __u32 sample;
        ssize_t n = read(fd, &sample, sizeof(sample));
        if (n < 0) {
            if (errno == EINTR)
                continue; // Interrupted by signal, try again
            std::cerr << "Read error: " << std::strerror(errno) << std::endl;
            break;
        }
        if (n == 0) {
            std::cerr << "EOF from device" << std::endl;
            break;
        }
        if (n != sizeof(sample)) {
            std::cerr << "Short read (" << n << " bytes), expected "
                      << sizeof(sample) << " bytes" << std::endl;
            continue;
        }

        // Decode according to lirc.h
        __u32 duration = sample & LIRC_VALUE_MASK;  // duration in microseconds
        __u32 type     = sample & LIRC_MODE2_MASK;  // event type

        switch (type) {
            case LIRC_MODE2_PULSE:
                std::cout << "PULSE   " << duration << " us" << std::endl;
                break;
            case LIRC_MODE2_SPACE:
                std::cout << "SPACE   " << duration << " us" << std::endl;
                break;
            case LIRC_MODE2_FREQUENCY:
                std::cout << "FREQ    " << duration << " Hz" << std::endl;
                break;
            case LIRC_MODE2_TIMEOUT:
                std::cout << "TIMEOUT " << duration << " us" << std::endl;
                break;

#ifdef LIRC_MODE2_OVERFLOW   // This macro exists only in newer kernels
            case LIRC_MODE2_OVERFLOW:
                std::cout << "OVERFLOW" << std::endl;
                break;
#endif

            default:
                std::cout << "UNKNOWN sample=0x"
                          << std::hex << sample << std::dec << std::endl;
                break;
        }
    }

    close(fd);
    return 0;
}
