/* Native regression checks for the 2.0.40 MkLinux port (GNU C / glibc 2.1). */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utime.h>

static int failures;
static void check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}
static int child_ok(pid_t pid)
{
    int status;
    return pid > 0 && waitpid(pid, &status, 0) == pid &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
static void signal_and_exec(void)
{
    pid_t pid;
    char *large;
    char *args[3];
    int fd;
    void *mapped;
    long result;
    pid = fork();
    if (pid == 0) {
        signal(SIGURG, SIG_DFL);
        kill(getpid(), SIGURG);
        _exit(0);
    }
    check(child_ok(pid), "SIGURG default action ignores signal");
    pid = fork();
    if (pid == 0) {
        execl("/bin/true", "true", (char *)0);
        _exit(1);
    }
    check(child_ok(pid), "fork and exec");
    large = malloc(256 * 1024);
    if (!large) exit(2);
    memset(large, 'x', 256 * 1024 - 1); large[256 * 1024 - 1] = 0;
    args[0] = "true"; args[1] = large; args[2] = NULL;
    pid = fork();
    if (pid == 0) {
        execv("/bin/true", args);
        _exit(errno == E2BIG ? 0 : 1);
    }
    check(child_ok(pid), "oversized exec argument returns E2BIG");
    free(large);
    fd = open("/proc/self/mem", O_RDONLY);
    mapped = fd < 0 ? MAP_FAILED : mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    check(fd >= 0 && mapped == MAP_FAILED && errno == ENODEV,
          "/proc/self/mem mmap disabled");
    if (mapped != MAP_FAILED) munmap(mapped, 4096);
    if (fd >= 0) close(fd);
    errno = 0;
    result = syscall(SYS_clone, 0x00200000UL | SIGCHLD, 0);
    if (result == 0) _exit(1);
    check(result == -1 && errno == EINVAL, "clone rejects internal CLONING_KERNEL flag");
    if (result > 0) child_ok(result);
    errno = 0;
    result = syscall(SYS_clone, 0x1000UL | SIGCHLD, 0);
    if (result == 0) _exit(1);
    check(result == -1 && errno == EINVAL, "clone rejects user CLONE_PID flag");
    if (result > 0) child_ok(result);
}
static void unix_rights(void)
{
    int sv[2], fd, got = -1, n, ok = 1;
    struct msghdr msg;
    struct iovec vec;
    struct cmsghdr *cmsg;
    union { struct cmsghdr align; char bytes[128]; } control;
    char byte = 'F', buf[4];
    fd = open("rights", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        check(0, "AF_UNIX setup"); return;
    }
    ok &= write(fd, "data", 4) == 4 && lseek(fd, 0, SEEK_SET) == 0;
    memset(&msg, 0, sizeof msg); memset(&control, 0, sizeof control);
    vec.iov_base = &byte; vec.iov_len = 1;
    msg.msg_iov = &vec; msg.msg_iovlen = 1;
    msg.msg_control = control.bytes; msg.msg_controllen = CMSG_SPACE(sizeof(int));
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof fd);
    ok &= sendmsg(sv[0], &msg, 0) == 1;
    close(fd);
    memset(&control, 0, sizeof control);
    msg.msg_controllen = sizeof control.bytes; byte = 0;
    n = recvmsg(sv[1], &msg, 0);
    cmsg = CMSG_FIRSTHDR(&msg);
    if (n == 1 && byte == 'F' && cmsg && cmsg->cmsg_level == SOL_SOCKET &&
        cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
        memcpy(&got, CMSG_DATA(cmsg), sizeof got);
    ok &= got >= 0 && read(got, buf, 4) == 4 && !memcmp(buf, "data", 4);
    check(ok, "AF_UNIX descriptor passing survives sender close");
    if (got >= 0) close(got);
    close(sv[0]); close(sv[1]); unlink("rights");
}
static void filesystems(void)
{
    char dir[32], path[64], block[4096], readback[4096];
    int i, j, fd, ok = 1;
    struct stat st;
    memset(block, 0xa5, sizeof block);
    for (i = 0; i < 128; i++) {
        sprintf(dir, "dir%03d", i); sprintf(path, "%s/data", dir);
        if (mkdir(dir, 0700) || (fd = open(path, O_CREAT | O_RDWR, 0600)) < 0) {
            ok = 0; break;
        }
        for (j = 0; j < 20; j++)
            if (write(fd, block, sizeof block) != sizeof block) ok = 0;
        if (fsync(fd) || lseek(fd, 16 * sizeof block, SEEK_SET) < 0 ||
            read(fd, readback, sizeof readback) != sizeof readback ||
            memcmp(block, readback, sizeof block)) ok = 0;
        if (ftruncate(fd, 123) || fstat(fd, &st) || st.st_size != 123) ok = 0;
        close(fd);
    }
    sync();
    check(ok && i == 128, "ext2 allocation across 128 directories, indirect I/O and truncate");
    ok = rename("dir000/data", "dir001/renamed") == 0 &&
         link("dir001/renamed", "hardlink") == 0 &&
         symlink("hardlink", "symlink") == 0 && stat("symlink", &st) == 0 &&
         st.st_size == 123;
    check(ok, "ext2 cross-directory rename and hard/symbolic links");
    check(mkfifo("fifo", 0600) == 0 && lstat("fifo", &st) == 0 && S_ISFIFO(st.st_mode),
          "ext2 FIFO creation");
    check(mknod("null", S_IFCHR | 0600, makedev(1, 3)) == 0 &&
          lstat("null", &st) == 0 && S_ISCHR(st.st_mode), "ext2 device-node creation");
    unlink("null"); unlink("fifo"); unlink("symlink"); unlink("hardlink");
    unlink("dir001/renamed");
    for (i = 0; i < 128; i++) {
        sprintf(dir, "dir%03d", i); sprintf(path, "%s/data", dir);
        unlink(path); rmdir(dir);
    }
}
static void timestamp_permissions(void)
{
    int fd;
    pid_t pid;
    fd = open("owned", O_CREAT | O_WRONLY, 0444);
    if (fd < 0) { check(0, "utime setup"); return; }
    close(fd);
    chown("owned", 65534, 65534);
    chmod("owned", 0444);
    chmod(".", 0755);
    pid = fork();
    if (pid == 0) {
        if (setgid(65534) || setuid(65534)) _exit(1);
        _exit(utime("owned", NULL) == 0 && utimes("owned", NULL) == 0 ? 0 : 1);
    }
    check(child_ok(pid), "utime and utimes permit owner of read-only file");
    unlink("owned");
}
int main(void)
{
    char path[128];
    setbuf(stdout, NULL);
    alarm(180);
    sprintf(path, "/var/tmp/mklinux-2.0.40-check-%ld", (long)getpid());
    if (mkdir(path, 0755) || chdir(path)) { perror(path); return 2; }
    signal_and_exec(); unix_rights(); filesystems(); timestamp_permissions();
    chdir("/"); rmdir(path);
    printf("RESULT: %d failures\n", failures);
    return failures != 0;
}
