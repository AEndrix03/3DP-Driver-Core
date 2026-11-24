#include "core/serial/LinuxBaud.hpp"

#ifdef __linux__

// <asm/termbits.h> ridefinisce le strutture di <termios.h>: per questo la funzione
// sta in un file a parte, che non include altro.
#include <asm/termbits.h>
#include <sys/ioctl.h>

namespace core {
    bool setLinuxBaud(int fd, unsigned baud) {
        struct termios2 tty{};
        if (ioctl(fd, TCGETS2, &tty) != 0) return false;
        tty.c_cflag &= ~static_cast<tcflag_t>(CBAUD);
        tty.c_cflag |= BOTHER; // velocita' data come numero, non come una delle costanti B...
        tty.c_ispeed = baud;
        tty.c_ospeed = baud;
        return ioctl(fd, TCSETS2, &tty) == 0;
    }
}

#else

namespace core {
    bool setLinuxBaud(int, unsigned) { return false; }
}

#endif
