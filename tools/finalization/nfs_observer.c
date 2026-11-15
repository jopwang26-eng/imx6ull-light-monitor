/*
 * 静态观察程序：子进程运行RAM中的应用，日志路径仍可指向NFS。
 * 父进程从管道收集输出，同时写串口和RAM报告；定时提交STATUS/STOP/START，
 * 到期发送SIGINT。最多额外等90秒，记录退出是否受存储阻塞影响。
 * 此程序本身不修改网络、NFS服务或驱动。
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop_handler(int number) { (void)number; interrupted = 1; }

static uint64_t milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        perror("observer clock");
        exit(2);
    }
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int write_all(int fd, const char *buffer, size_t size)
{
    ssize_t count;
    while (size) {
        count = write(fd, buffer, size);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (!count) return -1;
        buffer += count;
        size -= (size_t)count;
    }
    return 0;
}

static void report(int fd, const char *buffer, size_t size)
{
    if (write_all(fd, buffer, size) < 0 || write_all(STDOUT_FILENO, buffer, size) < 0) {
        perror("observer report");
        interrupted = 1;
    }
}

/*
 * path为NFS就绪文件路径。先写临时文件再rename，只有完整发布才触发Ubuntu。
 * 成功返回0，失败返回正errno；该动作发生在网络中断之前且仅执行一次。
 */
static int publish_ready(const char *path)
{
    char temporary[4096];
    int fd, error = 0;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
        return ENAMETOOLONG;
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return errno;
    if (write_all(fd, "ready\n", 6) < 0) error = errno ? errno : EIO;
    if (close(fd) < 0 && !error) error = errno;
    if (!error && rename(temporary, path) < 0) error = errno;
    if (error) unlink(temporary);
    return error;
}

int main(int argc, char **argv)
{
    int input[2], output[2], log_fd, status = 0, child_done = 0;
    int sent_stop = 0, sent_start = 0, sent_interrupt = 0;
    int armed = 0, arm_error;
    const char *ready_path = getenv("LM_NFS_READY_FILE");
    pid_t child, result;
    char *end, buffer[4096];
    long seconds;
    uint64_t started, elapsed, next_status = 1000, duration;
    struct pollfd descriptor;
    struct sigaction action;
    ssize_t count;
    int length;

    if (argc != 5 && argc != 8) {
        fprintf(stderr, "Usage: nfs_observer APP LOG_DIR SECONDS REPORT [SENSOR BUZZER KEY]\n");
        return 2;
    }
    errno = 0;
    seconds = strtol(argv[3], &end, 10);
    if (errno || !*argv[3] || *end || seconds < 2 || seconds > 120)
        return 2;
    duration = (uint64_t)seconds * 1000;
    log_fd = open(argv[4], O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (log_fd < 0 || pipe(input) < 0 || pipe(output) < 0) {
        perror("observer prepare");
        return 2;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        perror("observer signals");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN); /* 子进程已退出时管道写失败不会杀死观察者。 */
    started = milliseconds();
    child = fork();
    if (child < 0) { perror("observer fork"); return 2; }
    if (!child) {
        char *arguments[] = { argv[1], "-i", "200", "-l", argv[2],
                              "-d", argc == 8 ? argv[5] : "/dev/lightap3216c",
                              "-b", argc == 8 ? argv[6] : "/dev/lightbeep",
                              "-k", argc == 8 ? argv[7] : "/dev/lightkey", NULL };
        if (dup2(input[0], STDIN_FILENO) < 0 || dup2(output[1], STDOUT_FILENO) < 0 ||
            dup2(output[1], STDERR_FILENO) < 0)
            _exit(126);
        close(input[0]); close(input[1]); close(output[0]); close(output[1]); close(log_fd);
        execv(argv[1], arguments);
        perror("observer exec");
        _exit(127);
    }
    close(input[0]); close(output[1]);
    descriptor.fd = output[0]; descriptor.events = POLLIN;
    length = snprintf(buffer, sizeof(buffer), "OBSERVER_READY child=%ld duration_s=%ld\n", (long)child, seconds);
    report(log_fd, buffer, (size_t)length);
    for (;;) {
        elapsed = milliseconds() - started;
        result = waitpid(child, &status, WNOHANG);
        if (result == child) { child_done = 1; break; }
        if (result < 0 && errno != EINTR) { perror("observer waitpid"); break; }
        /*
         * 40秒观察时在第34秒发布标记，使12秒中断覆盖第40秒的退出请求。
         * 主机预先等待这个标记，用户不必在两个终端之间赶时间。
         */
        if (ready_path && !armed && !interrupted &&
            elapsed >= (duration > 6000 ? duration - 6000 : duration / 2)) {
            arm_error = publish_ready(ready_path);
            length = snprintf(buffer, sizeof(buffer), "OBSERVER_ARMED elapsed_ms=%llu error=%d\n",
                              (unsigned long long)elapsed, arm_error);
            report(log_fd, buffer, (size_t)length);
            armed = 1;
            if (arm_error) interrupted = 1;
        }
        if (elapsed >= next_status && !sent_interrupt) {
            write_all(input[1], "STATUS\n", 7);
            next_status = elapsed + 2000;
        }
        if (!sent_stop && elapsed >= duration / 2) {
            write_all(input[1], "STOP\nSTATUS\n", 12);
            sent_stop = 1;
        }
        if (!sent_start && elapsed >= duration / 2 + 1000) {
            write_all(input[1], "START\n", 6);
            sent_start = 1;
        }
        if (!sent_interrupt && (interrupted || elapsed >= duration)) {
            kill(child, SIGINT);
            length = snprintf(buffer, sizeof(buffer), "OBSERVER_SIGINT elapsed_ms=%llu\n", (unsigned long long)elapsed);
            report(log_fd, buffer, (size_t)length);
            sent_interrupt = 1;
        }
        if (elapsed >= duration + 90000) break;
        if (poll(&descriptor, 1, 100) > 0) {
            count = read(output[0], buffer, sizeof(buffer));
            if (count > 0) report(log_fd, buffer, (size_t)count);
        }
    }
    close(input[1]);
    if (child_done) {
        /* 子进程已结束，所有写端已关闭，可排空管道而不等待新输出。 */
        while ((count = read(output[0], buffer, sizeof(buffer))) > 0)
            report(log_fd, buffer, (size_t)count);
    }
    length = snprintf(buffer, sizeof(buffer),
                      "OBSERVER_EXIT child_done=%d child_code=%d elapsed_ms=%llu child_pid=%ld\n",
                      child_done, child_done ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)) : -1,
                      (unsigned long long)(milliseconds() - started), (long)child);
    report(log_fd, buffer, (size_t)length);
    close(output[0]);
    if (fsync(log_fd) < 0 || close(log_fd) < 0) return 2;
    /* 超时不宣称子进程已退出；报告保留PID供恢复后检查。 */
    return child_done && WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}
