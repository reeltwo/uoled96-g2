
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

static int reset_serial(int fd)
{
    for (int is_on = 0; is_on < 2; is_on++)
    {
        unsigned int ctl;
        if (ioctl(fd, TIOCMGET, &ctl) == 0)
        {
            if (is_on)
            {
                /* Clear DTR and RTS */
                ctl &= ~(TIOCM_DTR);
            }
            else
            {
                /* Set DTR and RTS */
                ctl |= (TIOCM_DTR);
            }
            if (ioctl(fd, TIOCMSET, &ctl) != 0)
                printf("ioctl1 failed\n");
            {
                int e = usleep(100*1000);
                if (e != 0) printf("errno : %d\n", errno);
            }
        }
        else
        {
            printf("ioctl failed\n");
        }
    }
    sleep(1);
    struct timeval timeout;
    fd_set rfds;
    int nfds;
    int rc;
    unsigned char buf;

    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;

    while (1)
    {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

    reselect:
        nfds = select(fd + 1, &rfds, NULL, NULL, &timeout);
        if (nfds == 0)
        {
            break;
        }
        else if (nfds == -1)
        {
            if (errno == EINTR)
            {
                goto reselect;
            }
            else
            {
                return 1;
            }
        }
        rc = read(fd, &buf, 1);
        if (rc < 0)
        {
            return 1;
        }
    }
    return 0;
}

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
    speed_t speed = serial_baud_lookup((baud != 0) ? baud : 115200);
    if (speed == 0)
    {
        // Non-standard speed
    }

#ifdef __APPLE__
    fd = open(sport, O_RDWR | O_NOCTTY | O_NONBLOCK);
#else
    fd = open(sport, O_RDWR | O_NOCTTY);
#endif
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
    const char* ID_STR = "uOLED96-G2\n96x64\n";
    int trycount = 0;
    if (argc < 2)
    {
        fprintf(stderr, "%s: [-v] file.4xe [device] [baud rate]", argv[0]);
        return 1;
    }
    const char* fname = argv[1];
    const char* sport = (argc >= 3) ? argv[2] : "/dev/ttyUSB0";
    int baud = (argc == 4) ? atoi(argv[3]) : 115200;
    int fd = -1;

    FILE *f = fopen(fname, "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    int numchunks = (fsize + 63) / 64;
    unsigned char* prog = (unsigned char*)malloc(numchunks * 64);
    memset(prog, 0xFF, numchunks * 64);
    fread(prog, 1, fsize, f);
    fclose(f);

    // for (int i = 0; i < numchunks; i++)
    // {
    //     unsigned char chksum = 0;
    //     printf("[%d]\n", i);
    //     for (int x = 0; x < 64; x++)
    //     {
    //         printf("%02x ", prog[i*64+x]);
    //         chksum += prog[i*64+x];
    //         if (((x+1) % 16) == 0)
    //             printf("\n");
    //     }
    //     chksum = ~chksum + 1;
    //     printf("%02X\n", chksum);
    // }

    while (fd == -1)
    {
        fd = open_serial(sport, baud);

        if (fd == -1)
        {
            sleep(5);
            if (trycount++ < 10)
                printf("Trying again ...\n");
        }
    }
    reset_serial(fd);
    {
        int n;
        char buf[65];
        if (write(fd, "4dgl", 4) != 4)
        {
            printf("Failed to write at %d\n", __LINE__);
            goto err;
        }

        n = read(fd, buf, 1);
        if (n != 1 || buf[0] != 'G')
        {
            printf("Failed to read at %d\n", __LINE__);
            goto err;
        }
        if (write(fd, "V", 1) != 1)
        {
            printf("Failed to write at %d\n", __LINE__);
            goto err;
        }
        n = read(fd, buf, 65);
        if (n == -1)
        {
            printf("Failed to read at %d\n", __LINE__);
            goto err;
        }
        if (n > 0 && buf[n-1] == 6)
            buf[n-1] = 0;
        if (strncmp(buf, ID_STR, strlen(ID_STR)) != 0)
        {
            printf("Unknown device\n");
            goto err;
        }
        printf("Found uOLED device\n");
    }

    reset_serial(fd);
    {
        int n;
        char buf[256];
        if (write(fd, "4dgl", 4) != 4)
        {
            printf("Failed to write at %d\n", __LINE__);
            goto err;
        }

        n = read(fd, buf, 1);
        if (n != 1 || buf[0] != 'G')
        {
            printf("Failed to read at %d\n", __LINE__);
            goto err;
        }
        buf[0] = 'L';
        buf[1] = numchunks;
        if (write(fd, buf, 2) != 2)
        {
            printf("Failed to write at %d\n", __LINE__);
            goto err;
        }
        n = read(fd, buf, 1);
        if (n != 1 || buf[0] != 6)
        {
            printf("Failed to read at %d\n", __LINE__);
            goto err;
        }
        for (int i = 0; i < numchunks; i++)
        {
            unsigned char chksum = 0;
            for (int x = 0; x < 64; x++)
            {
                buf[x] = prog[i*64+x];
                chksum += prog[i*64+x];
            }
            buf[64] = ~chksum + 1;
            if (write(fd, buf, 65) != 65)
            {
                printf("Failed to write at %d\n", __LINE__);
                goto err;
            }
            if (i+1 < numchunks)
            {
                n = read(fd, buf, 1);
                if (n != 1 || buf[0] != 0)
                {
                    printf("Failed to read at %d\n", __LINE__);
                    goto err;
                }
            }
            else
            {
                n = read(fd, buf, 2);
                if (n != 2 || buf[0] != 0 || buf[1] != 6)
                {
                    printf("Failed to read at %d\n", __LINE__);
                    goto err;
                }
            }
        }
    }
err:
    close_serial(fd);
    return 0;
}
