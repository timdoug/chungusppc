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
    int formats[] = {AFMT_S16_BE, AFMT_S16_LE, AFMT_U8, AFMT_S8,
                     AFMT_U16_BE, AFMT_U16_LE};
    const char *names[] = {"S16 BE", "S16 LE", "U8", "S8", "U16 BE", "U16 LE"};
    unsigned char samples[8192 * 4];
    int r, f, repeat, i, channel, wanted_channels;
    alarm(180);
    for (wanted_channels = 1; wanted_channels <= 2; ++wanted_channels)
    for (r = 0; r < 2; ++r) for (f = 0; f < 6; ++f) {
        int fd = open("/dev/dsp", O_WRONLY);
        int format = formats[f], channels = wanted_channels, rate = rates[r];
        int eight_bit = f == 2 || f == 3;
        int little_endian = f == 1 || f == 5;
        int sample_bytes = eight_bit ? 1 : 2;
        int length = 8192 * wanted_channels * sample_bytes;
        if (fd < 0) fail("open /dev/dsp");
        if (ioctl(fd, SNDCTL_DSP_SETFMT, &format) < 0) fail("format");
        if (ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) < 0) fail("channels");
        if (ioctl(fd, SNDCTL_DSP_SPEED, &rate) < 0) fail("rate");
        if (format != formats[f] || channels != wanted_channels || rate != rates[r]) {
            fprintf(stderr, "unexpected format=%d channels=%d rate=%d\n", format, channels, rate);
            return 1;
        }
        for (i = 0; i < 8192; ++i) for (channel = 0; channel < channels; ++channel) {
            short value = channel ? ((i % 128) - 64) * 32 : ((i % 64) - 32) * 64;
            unsigned short encoded = (unsigned short)value;
            unsigned char *p = samples + (i * channels + channel) * sample_bytes;
            if (eight_bit) p[0] = (encoded >> 8) ^ (f == 2 ? 0x80 : 0);
            else {
                if (f == 4 || f == 5) encoded ^= 0x8000;
                p[little_endian ? 1 : 0] = encoded >> 8;
                p[little_endian ? 0 : 1] = encoded & 255;
            }
        }
        for (repeat = 0; repeat < 2; ++repeat) {
            int done = 0;
            while (done < length) {
                int n = write(fd, samples + done, length - done);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) fail("write");
                done += n;
            }
            if (ioctl(fd, SNDCTL_DSP_SYNC, 0) < 0) fail("sync");
            sleep(1);
        }
        if (close(fd) < 0) fail("close");
        printf("PASS %d Hz %s %s, including restart\n", rate, names[f],
               channels == 1 ? "mono" : "stereo");
        fflush(stdout);
    }
    return 0;
}
