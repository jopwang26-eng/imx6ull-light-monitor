/* 仅用于原生LD_PRELOAD测试：在真实系统调用边界控制关闭失败及在途读取。 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int matches(int fd, const char *environment)
{
    char link[64], path[4096];
    const char *expected = getenv(environment);
    ssize_t length;
    if (!expected)
        return 0;
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    length = readlink(link, path, sizeof(path) - 1);
    if (length < 0)
        return 0;
    path[length] = '\0';
    return !strcmp(path, expected);
}

ssize_t write(int fd, const void *data, size_t count)
{
    ssize_t (*real_write)(int, const void *, size_t) = dlsym(RTLD_NEXT, "write");
    const char *gate = getenv("LM_TEST_FAIL_OFF");
    if (count == 1 && *(const unsigned char *)data == 0 && gate &&
        access(gate, F_OK) == 0 && matches(fd, "LM_TEST_BUZZER_PATH")) {
        errno = EIO;
        return -1;
    }
    return real_write(fd, data, count);
}

ssize_t read(int fd, void *data, size_t count)
{
    ssize_t (*real_read)(int, void *, size_t) = dlsym(RTLD_NEXT, "read");
    const char *gate = getenv("LM_TEST_READ_GATE");
    const char *marker = getenv("LM_TEST_READ_READY");
    int notify;
    if (count == 2 && gate && access(gate, F_OK) == 0 &&
        matches(fd, "LM_TEST_SENSOR_PATH")) {
        if (marker) {
            notify = open(marker, O_WRONLY | O_CREAT, 0600);
            if (notify >= 0)
                close(notify);
        }
        while (access(gate, F_OK) == 0)
            usleep(10000);
    }
    return real_read(fd, data, count);
}
