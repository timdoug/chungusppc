/* OSS playback check for MkLinux. Build with: gcc -O2 -o audio-test mklinux-audio-test.c */
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static void fail(const char *what) { perror(what); exit(1); }

int main(void)
{
    int rates[] = {44100, 22050};
    int formats[] = {AFMT_S16_BE, AFMT_S16_LE};
    unsigned char samples[8192 * 4];
    int r, f, repeat, i, channel;
    alarm(60);
    for (r = 0; r < 2; ++r) for (f = 0; f < 2; ++f) {
        int fd = open("/dev/dsp", O_WRONLY);
        int format = formats[f], channels = 2, rate = rates[r];
        if (fd < 0) fail("open /dev/dsp");
        if (ioctl(fd, SNDCTL_DSP_SETFMT, &format) < 0) fail("format");
        if (ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) < 0) fail("channels");
        if (ioctl(fd, SNDCTL_DSP_SPEED, &rate) < 0) fail("rate");
        if (format != formats[f] || channels != 2 || rate != rates[r]) {
            fprintf(stderr, "unexpected format=%d channels=%d rate=%d\n", format, channels, rate);
            return 1;
        }
        for (i = 0; i < 8192; ++i) for (channel = 0; channel < 2; ++channel) {
            short value = channel ? ((i % 128) - 64) * 32 : ((i % 64) - 32) * 64;
            unsigned char *p = samples + i * 4 + channel * 2;
            p[f ? 1 : 0] = (unsigned short)value >> 8;
            p[f ? 0 : 1] = value & 255;
        }
        for (repeat = 0; repeat < 2; ++repeat) {
            int done = 0;
            while (done < sizeof(samples)) {
                int n = write(fd, samples + done, sizeof(samples) - done);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) fail("write");
                done += n;
            }
            if (ioctl(fd, SNDCTL_DSP_SYNC, 0) < 0) fail("sync");
            sleep(1);
        }
        if (close(fd) < 0) fail("close");
        printf("PASS %d Hz signed 16-bit %s stereo, including restart\n", rate, f ? "LE" : "BE");
        fflush(stdout);
    }
    return 0;
}
