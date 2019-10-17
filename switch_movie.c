
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/file.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

static speed_t serial_baud_lookup(long baud)
{
    struct baud_mapping
    {
        long baud;
        speed_t speed;
    };
    static struct baud_mapping baud_lookup_table [] =
    {
        { 1200,   B1200 },
        { 2400,   B2400 },
        { 4800,   B4800 },
        { 9600,   B9600 },
        { 19200,  B19200 },
        { 38400,  B38400 },
        { 57600,  B57600 },
        { 115200, B115200 },
        { 230400, B230400 },
        { 0,      0 }                 /* Terminator. */
    };
    for (struct baud_mapping *map = baud_lookup_table; map->baud; map++)
    {
        if (map->baud == baud)
            return map->speed;
    }
    return baud;
}

static int open_serial(const char* serial_port, unsigned baud)
{
    int fd;
    int rc;
    struct termios termios;
    const char* sport = (serial_port != NULL) ? serial_port : "/dev/ttyUSB0";
    speed_t speed = serial_baud_lookup((baud != 0) ? baud : 9600);
    if (speed == 0)
    {
        // Non-standard speed
    }

    fd = open(sport, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    /* Make sure device is of tty type */
    if (!isatty(fd))
    {   
        close(fd);
        return -1;
    }
    /* Lock device file */
    if ((flock(fd, LOCK_EX | LOCK_NB) == -1) && (errno == EWOULDBLOCK))
    {
        close(fd);
        return -1;
    }

    rc = tcgetattr(fd, &termios);
    if (rc < 0)
    {
        close(fd);
        return -1;
    }

    termios.c_iflag = IGNBRK;
    termios.c_oflag = 0;
    termios.c_lflag = 0;
    termios.c_cflag = (CS8 | CREAD | CLOCAL);
    termios.c_cc[VMIN]  = 1;
    termios.c_cc[VTIME] = 0;

    rc = cfsetospeed(&termios, speed);
    if (rc == -1)
    {
        close(fd);
        return -1;
    }
    rc = cfsetispeed(&termios, speed);
    if (rc == -1)
    {
        close(fd);
        return -1;
    }
    // 8 data bits
    termios.c_cflag &= ~CSIZE;
    termios.c_cflag |= CS8;

    // No flow control
    termios.c_cflag &= ~CRTSCTS;
    termios.c_iflag &= ~(IXON | IXOFF | IXANY);

    // 1 stop bit
    termios.c_cflag &= ~CSTOPB;

    // No parity
    termios.c_cflag &= ~PARENB;

    /* Control, input, output, local modes for tty device */
    termios.c_cflag |= CLOCAL | CREAD;
    termios.c_oflag = 0;
    termios.c_lflag = 0;

    /* Control characters */
    termios.c_cc[VTIME] = 0; // Inter-character timer unused
    termios.c_cc[VMIN]  = 1; // Blocking read until 1 character received

    rc = tcsetattr(fd, TCSANOW, &termios);
    if (rc == -1)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static int close_serial(int fd)
{
    return (fd != -1) ? close(fd) : -1;
}


int main(int argc, const char* argv[])
{
    int trycount = 0;
    const char* sport = (argc >= 3) ? argv[2] : "/dev/ttyUSB0";
    int fd = -1;
    int movieIndex = atoi(argv[1]);

    while (fd == -1)
    {
        fd = open_serial(sport, 9600);

        if (fd == -1)
        {
            sleep(5);
            if (trycount++ < 10)
                printf("Trying again ...\n");
        }
    }
    int ch;
    unsigned char movieSelection = (unsigned char)movieIndex;
    write(fd, &movieSelection, 1);
    sleep(1000);
err:
    close_serial(fd);
    return 0;
}
