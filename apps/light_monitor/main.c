/* 前台串口控制入口：主线程独占终端输出，采集在线程内独立运行。 */
#define _POSIX_C_SOURCE 200809L
#include "light_monitor.h"
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;

static void on_signal(int number)
{
    (void)number;
    interrupted = 1; /* 信号处理器只修改标志；不调用printf、锁或设备操作。 */
}

static int parse_number(const char *text, unsigned int minimum,
                        unsigned int maximum, unsigned int *out)
{
    char *end;
    unsigned long number;
    const char *cursor;
    /* 仅接受十进制数字，拒绝空串、符号、空格、尾随字符及溢出。 */
    if (!*text)
        return -1;
    for (cursor = text; *cursor; ++cursor)
        if (*cursor < '0' || *cursor > '9')
            return -1;
    errno = 0;
    number = strtoul(text, &end, 10);
    if (errno || *end || number < minimum || number > maximum)
        return -1;
    *out = (unsigned int)number;
    return 0;
}

static void print_sample(const struct lm_snapshot *state)
{
    if (state->valid)
        printf("SAMPLE attempt=%" PRIu64 " elapsed_ms=%" PRIu64
               " valid=1 ALS_RAW=%u missed=%" PRIu64 " alarm=%s\n",
               state->attempts, state->elapsed_ms, state->raw, state->missed,
               lm_alarm_name(state));
    else
        printf("SAMPLE attempt=%" PRIu64 " elapsed_ms=%" PRIu64
               " valid=0 ALS_RAW=NA errno=%d errors=%" PRIu64 "\n",
               state->attempts, state->elapsed_ms, state->last_errno, state->errors);
}

int main(int argc, char **argv)
{
    const char *device = "/dev/lightap3216c";
    const char *log_directory = "./logs";
    const char *buzzer_path = "/dev/lightbeep", *key_path = "/dev/lightkey";
    unsigned int interval = 1000;
    unsigned int log_limit = LM_LOG_DEFAULT_LIMIT;
    struct lm_logger logger;
    struct lm_sampler sampler;
    struct lm_snapshot state;
    struct sigaction action;
    sigset_t blocked, previous;
    struct pollfd inputs[2] = { { .fd = STDIN_FILENO, .events = POLLIN },
                                { .fd = -1, .events = POLLIN } };
    struct timespec now;
    struct stat key_type;
    uint64_t key_retry = 0, now_ms, displayed_control_errors = 0;
    unsigned char key_event;
    char bytes[128], line[128], reply[1024];
    size_t used = 0;
    uint64_t displayed = 0;
    uint64_t displayed_log_errors = 0;
    int displayed_log_failed = 0;
    int option, error, result = 0, dropping = 0, ending = 0, ready;
    ssize_t count, index;

    /* getopt读取-d设备和-i毫秒周期；错误参数退出2，不启动采集线程。 */
    while ((option = getopt(argc, argv, "d:i:l:r:b:k:h")) != -1) {
        if (option == 'd')
            device = optarg;
        else if (option == 'l')
            log_directory = optarg;
        else if (option == 'b')
            buzzer_path = optarg;
        else if (option == 'k')
            key_path = optarg;
        else if (option == 'r' && !parse_number(optarg, 128, LM_LOG_DEFAULT_LIMIT, &log_limit))
            continue;
        else if (option == 'i' && !parse_number(optarg, 100, 60000, &interval))
            continue;
        else {
            fprintf(option == 'h' ? stdout : stderr,
                    "Usage: light_monitor [-d DEVICE] [-b BUZZER] [-k KEY|none]"
                    " [-i 100..60000] [-l LOG_DIR] [-r 128..10485760]\n");
            return option == 'h' ? 0 : 2;
        }
    }
    if (optind != argc)
        return 2;
    /* stdout按行刷新，重定向时测试进程也能及时读到响应。 */
    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        return 1;
    }
    /*
     * 创建线程前屏蔽退出信号，工作线程继承屏蔽集。
     * 主线程随后恢复原屏蔽集，保证只有主线程处理退出信号。
     * pthread系列函数返回错误码本身，不通过errno返回。
     */
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    error = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (error) {
        fprintf(stderr, "signal mask: %s\n", strerror(error));
        return 1;
    }
    error = lm_logger_start(&logger, log_directory, log_limit);
    if (error) {
        fprintf(stderr, "logger start: %s\n", strerror(error));
        return 1;
    }
    error = lm_sampler_start(&sampler, device, interval, &logger, buzzer_path);
    if (error) {
        fprintf(stderr, "sampler start: %s\n", strerror(error));
        lm_logger_stop(&logger);
        return 1;
    }
    error = pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (error) {
        fprintf(stderr, "restore signal mask: %s\n", strerror(error));
        result = 1;
        goto stop;
    }
    printf("READY control-v1 interval_ms=%u commands=STATUS,START,STOP,SET_THRESHOLD,SET_INTERVAL"
           " alarm=DISABLED log_dir=%s log_limit=%u\n",
           interval, log_directory, log_limit);
    while (!interrupted && !ending) {
        lm_sampler_snapshot(&sampler, &state);
        if (state.fatal_errno) {
            fprintf(stderr, "sampler fatal: %s\n", strerror(state.fatal_errno));
            result = 1;
            break;
        }
        if (state.attempts != displayed) {
            print_sample(&state);
            displayed = state.attempts;
        }
        if (state.log.failures != displayed_log_errors ||
            state.log.failed != displayed_log_failed) {
            printf("LOG failed=%d errno=%d errors=%" PRIu64 " dropped=%" PRIu64 "\n",
                   state.log.failed, state.log.last_errno,
                   state.log.failures, state.log.dropped);
            displayed_log_errors = state.log.failures;
            displayed_log_failed = state.log.failed;
        }
        if (state.control_errors != displayed_control_errors) {
            printf("CONTROL errno=%d errors=%" PRIu64 " buzzer=%s\n",
                   state.control_errno, state.control_errors,
                   state.buzzer_known ? (state.buzzer_on ? "ON" : "OFF") : "UNKNOWN");
            displayed_control_errors = state.control_errors;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            perror("key clock");
            result = 1;
            break;
        }
        now_ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        if (strcmp(key_path, "none") && inputs[1].fd < 0 && now_ms >= key_retry) {
            inputs[1].fd = open(key_path, O_RDONLY | O_NONBLOCK);
            error = inputs[1].fd < 0 ? errno : 0;
            if (!error && fstat(inputs[1].fd, &key_type) < 0)
                error = errno;
            if (!error && !S_ISCHR(key_type.st_mode) && !S_ISFIFO(key_type.st_mode))
                error = EINVAL;
            if (error && inputs[1].fd >= 0) {
                close(inputs[1].fd);
                inputs[1].fd = -1;
            }
            lm_sampler_key_status(&sampler, 1, error);
            if (error)
                printf("KEY unavailable errno=%d (retry in 1s)\n", error);
            key_retry = now_ms + 1000;
        }
        /*
         * poll参数为监听fd数组、数量、超时毫秒。
         * 超时限制主线程检查信号与快照的等待，不控制采集节奏。
         */
        ready = poll(inputs, 2, 50);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll stdin");
            result = 1;
            break;
        }
        if (!ready)
            continue;
        if (inputs[1].fd >= 0 && inputs[1].revents) {
            /* poll不消费事件；read成功取得二进制1才提交一次切换。 */
            count = read(inputs[1].fd, &key_event, 1);
            error = count < 0 ? errno : (count == 1 && key_event == 1 ? 0 : EIO);
            if (!error) {
                error = lm_sampler_control(&sampler, LM_TOGGLE, 0, 0);
                lm_sampler_snapshot(&sampler, &state);
                printf("KEY action=TOGGLE state=%s control_errno=%d\n",
                       state.running ? "RUNNING" : "PAUSED", error);
            } else if (error != EAGAIN && error != EINTR) {
                lm_sampler_key_status(&sampler, 1, error);
                printf("KEY read failed errno=%d\n", error);
                close(inputs[1].fd);
                inputs[1].fd = -1;
                key_retry = now_ms + 1000;
            }
        }
        if (inputs[0].revents & (POLLERR | POLLNVAL)) {
            fprintf(stderr, "stdin unavailable\n");
            result = 1;
            break;
        }
        if (!(inputs[0].revents & (POLLIN | POLLHUP)))
            continue;
        /* HUP仍可能有未读管道数据，先read，读到EOF才正常退出。 */
        count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            perror("read stdin");
            result = 1;
            break;
        }
        if (!count) {
            ending = 1;
            break;
        }
        for (index = 0; index < count; ++index) {
            if (bytes[index] == '\n') {
                if (dropping) {
                    puts("ERR invalid_or_too_long_line");
                } else {
                    line[used] = '\0';
                    lm_command(line, &sampler, reply, sizeof(reply));
                    puts(reply);
                }
                dropping = 0;
                used = 0;
            } else if (!dropping) {
                if (!bytes[index] || used == sizeof(line) - 1) {
                    dropping = 1;
                    used = 0; /* 丢弃至换行，禁止截断后误执行命令。 */
                } else {
                    line[used++] = bytes[index];
                }
            }
        }
    }
stop:
    if (inputs[1].fd >= 0) {
        if (close(inputs[1].fd) < 0) {
            perror("close key");
            result = 1;
        }
    }
    /* 不强制取消可能正在执行的设备read，等待它自然完成后回收。 */
    error = lm_sampler_stop(&sampler);
    if (error) {
        fprintf(stderr, "sampler join: %s\n", strerror(error));
        return 1;
    }
    /* 先停止生产，再通知日志线程排空；存储失败时不会等5秒再退出。 */
    error = lm_logger_stop(&logger);
    if (error) {
        fprintf(stderr, "logger join: %s\n", strerror(error));
        return 1;
    }
    if (logger.stats.dropped || logger.stats.failed || logger.stats.uncertain)
        result = 1;
    if (sampler.state.control_errors || sampler.state.key_errors)
        result = 1;
    /* join后对象已无并发访问，可以直接读取最后统计。 */
    printf("EXIT state=STOPPED attempts=%" PRIu64 " successes=%" PRIu64
           " errors=%" PRIu64 " missed=%" PRIu64
           " log_saved=%" PRIu64 " log_dropped=%" PRIu64
           " log_errors=%" PRIu64 " log_failed=%d log_errno=%d log_uncertain=%" PRIu64
           " control_errors=%" PRIu64 " control_errno=%d buzzer=%s key_errors=%" PRIu64
           " superseded=%" PRIu64 "\n",
           sampler.state.attempts, sampler.state.successes,
           sampler.state.errors, sampler.state.missed, logger.stats.saved,
           logger.stats.dropped, logger.stats.failures, logger.stats.failed,
           logger.stats.last_errno, logger.stats.uncertain, sampler.state.control_errors,
           sampler.state.control_errno, sampler.state.buzzer_known ?
           (sampler.state.buzzer_on ? "ON" : "OFF") : "UNKNOWN",
           sampler.state.key_errors, sampler.state.superseded);
    return result;
}
