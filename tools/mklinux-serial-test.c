/* Binary serial round trip for MkLinux; pair with chungusppc-serial-test.py.
 * Build: gcc -O2 -Wall -o serial-test mklinux-serial-test.c
 * Run: ./serial-test /dev/ttyS0 (or /dev/ttyS1).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#define TEST_BYTES 16384

static void fail(const char *message) { perror(message); exit(1); }

static void transfer(int fd, unsigned char *bytes, int count, int writing)
{
    int done = 0;
    while (done < count) {
        fd_set fds;
        struct timeval timeout = {10, 0};
        int n;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        n = select(fd + 1, writing ? NULL : &fds, writing ? &fds : NULL,
                   NULL, &timeout);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) fail("select");
        if (!n) {
            fprintf(stderr, "%s timed out after %d of %d bytes\n",
                    writing ? "write" : "read", done, count);
            exit(1);
        }
        n = writing ? write(fd, bytes + done, count - done)
                    : read(fd, bytes + done, count - done);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (n < 0) fail("serial transfer");
        if (!n) {
            fprintf(stderr, "serial %s returned EOF after %d bytes\n",
                    writing ? "write" : "read", done);
            exit(1);
        }
        done += n;
    }
}

int main(int argc, char **argv)
{
    int fd, i;
    struct termios attr;
    unsigned char data[TEST_BYTES];
    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/ttyS0\n", argv[0]);
        return 1;
    }
    alarm(120);
    /* getty closes and reopens the line at startup. Old Mach read replies
     * must not hang up a later open of the same Linux tty. */
    for (i = 0; i < 8; ++i) {
        fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) fail("reopen serial port");
        if (close(fd)) fail("close serial port");
    }
    fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) fail("open serial port");
    if (tcgetattr(fd, &attr)) fail("tcgetattr");
    cfmakeraw(&attr);
    attr.c_cflag |= CLOCAL | CREAD;
    attr.c_cflag &= ~CRTSCTS;
    cfsetispeed(&attr, B38400);
    cfsetospeed(&attr, B38400);
    if (tcsetattr(fd, TCSANOW, &attr)) fail("tcsetattr");
    transfer(fd, (unsigned char *)"GO0", 3, 1);
    transfer(fd, data, TEST_BYTES, 0);
    for (i = 0; i < TEST_BYTES; ++i) {
        if (data[i] != (unsigned char)(i * 73 + 19)) {
            fprintf(stderr, "received byte %d differs\n", i);
            return 1;
        }
    }
    transfer(fd, data, TEST_BYTES, 1);
    if (tcdrain(fd)) fail("tcdrain");
    if (close(fd)) fail("close");
    printf("PASS %s: %d binary bytes in both directions at 38400 baud\n",
           argv[1], TEST_BYTES);
    return 0;
}
