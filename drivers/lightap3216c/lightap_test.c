/*
 * 光照原始值读取工具。默认读取 /dev/lightap3216c 的十个样本。
 * 两字节协议固定为低字节在前，不直接把内存转换为主机u16。
 * 读取失败或短帧退出并报告错误，不输出上一次成功的值冒充新值。
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void stop_handler(int signal_number)
{
    (void)signal_number;
    stopping = 1; /* 信号处理只写标志，清理由主循环完成。 */
}

/* 严格解析非负整数，拒绝负号、溢出、尾随字符及超范围值。 */
static int parse_number(const char *text, unsigned int limit, unsigned int *out)
{
    char *end;
    unsigned long value;

    if (!text[0] || text[0] == '-')
        return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value > limit)
        return -1;
    *out = (unsigned int)value;
    return 0;
}

static void usage(FILE *stream)
{
    fprintf(stream, "Usage: lightap_test [-d DEVICE] [-n COUNT] [-i MS]\n"
                    "COUNT: 0=until Ctrl+C, default=10; MS: 100..60000, default=1000\n");
}

int main(int argc, char **argv)
{
    const char *device = "/dev/lightap3216c";
    unsigned int count = 10, interval = 1000, sample = 0;
    struct sigaction action;
    struct timespec delay;
    unsigned char frame[2];
    ssize_t n;
    int option, fd, status = 0;

    while ((option = getopt(argc, argv, "d:n:i:h")) != -1) {
        switch (option) {
        case 'd': device = optarg; break;
        case 'n':
            if (parse_number(optarg, 1000000, &count)) {
                usage(stderr);
                return 2;
            }
            break;
        case 'i':
            if (parse_number(optarg, 60000, &interval) || interval < 100) {
                usage(stderr);
                return 2;
            }
            break;
        case 'h': usage(stdout); return 0;
        default: usage(stderr); return 2;
        }
    }
    if (optind != argc) {
        usage(stderr);
        return 2;
    }

    /* 兼容当前旧工具链，显式清零 sigaction，避免聚合初始化警告。 */
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open sensor");
        return 1;
    }

    while (!stopping && (!count || sample < count)) {
        n = read(fd, frame, sizeof(frame));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read sensor");
            status = 1;
            break;
        }
        if (n != (ssize_t)sizeof(frame)) {
            fprintf(stderr, "short sensor frame: got %ld, expected 2\n", (long)n);
            status = 1;
            break;
        }
        ++sample;
        printf("sample=%u ALS_RAW=%u\n", sample,
               (unsigned int)frame[0] | ((unsigned int)frame[1] << 8));
        if (count && sample == count)
            break;

        /* 采样节奏放在用户态；被信号中断后仅等待剩余时间。 */
        delay.tv_sec = interval / 1000;
        delay.tv_nsec = (long)(interval % 1000) * 1000000L;
        while (!stopping && nanosleep(&delay, &delay) < 0) {
            if (errno != EINTR) {
                perror("nanosleep");
                status = 1;
                goto out;
            }
        }
    }
out:
    if (close(fd) < 0) {
        perror("close sensor");
        status = 1;
    }
    return status;
}
