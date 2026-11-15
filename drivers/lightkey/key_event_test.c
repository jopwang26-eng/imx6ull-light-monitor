/*
 * key_event_test.c -- /dev/lightkey 事件接口测试程序。
 *
 * 这是用户空间程序，不是内核模块，使用交叉 gcc 单独编译。
 * 运行前退出 od 循环，先松开 KEY0；看到 WAIT 后再按键。
 * 每次正常按下间隔约一秒，长按不应重复报告。
 *
 * 检查：第二次 open 返回 EBUSY；无事件 read 返回 EAGAIN；
 * 无事件 poll 超时；poll 就绪不会消费事件；首次事件故意使用
 * 空缓冲区读取，确认 EFAULT 后仍能正常消费；消费后再次 EAGAIN。
 * Ctrl+C 只设置退出标志，由主流程关闭文件。
 */

#include <errno.h>    /* errno、EAGAIN、EBUSY、EFAULT、EINTR */
#include <fcntl.h>    /* open、O_RDONLY、O_NONBLOCK */
#include <poll.h>     /* poll、struct pollfd、就绪标志 */
#include <signal.h>   /* sigaction、sig_atomic_t */
#include <stdio.h>    /* printf、fprintf、perror */
#include <string.h>   /* memset：完整清零信号处理配置 */
#include <unistd.h>   /* read、close、ssize_t */

#define KEY_DEVICE "/dev/lightkey"

/* 信号处理函数与主流程之间使用 sig_atomic_t 传递退出请求。 */
static volatile sig_atomic_t stop_requested;

static void handle_sigint(int signo)
{
    (void)signo;
    /* 信号处理函数只设置标志，不调用 printf 或执行复杂清理。 */
    stop_requested = 1;
}

/*
 * 检查“没有待处理事件”的非阻塞读取行为。
 * 成功返回0，观察到不符合约定的结果返回-1。
 */
static int expect_empty(int fd)
{
    unsigned char event = 0;
    ssize_t n;
    int error;

    n = read(fd, &event, sizeof(event));
    error = (n < 0) ? errno : 0;

    if (n == -1 && (error == EAGAIN || error == EWOULDBLOCK))
        return 0;

    fprintf(stderr, "FAIL: expected EAGAIN, read=%ld data=%u errno=%d\n",
            (long)n, (unsigned int)event, error);
    return -1;
}

int main(void)
{
    struct sigaction action;
    struct pollfd pfd;
    unsigned char event = 0;
    unsigned int events = 0;
    ssize_t n;
    int fd;
    int other;
    int ret;
    int error;
    int first_event = 1;
    int status = 1;

    /* 按行刷新输出，串口上能及时看到 WAIT 和 PASS。 */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* 不设置 SA_RESTART，让 Ctrl+C 能中断下面的 poll 等待。 */
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigint;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    /* O_NONBLOCK：没有事件时 read 应立即返回 EAGAIN。 */
    fd = open(KEY_DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/lightkey");
        return 1;
    }

    /* 保持第一个文件打开，再尝试一次独立 open。 */
    other = open(KEY_DEVICE, O_RDONLY | O_NONBLOCK);
    error = (other < 0) ? errno : 0;
    if (other >= 0) {
        close(other);
        fprintf(stderr, "FAIL: second open succeeded\n");
        goto out;
    }
    if (error != EBUSY) {
        fprintf(stderr, "FAIL: second open errno=%d, expected EBUSY\n", error);
        goto out;
    }
    puts("PASS: second open -> EBUSY");

    /* 最多保留一个事件，先消费运行前可能残留的那一个。 */
    n = read(fd, &event, sizeof(event));
    error = (n < 0) ? errno : 0;
    if (n == 1 && event == 1) {
        puts("INFO: cleared one previously pending event");
    } else if (!(n == -1 && (error == EAGAIN || error == EWOULDBLOCK))) {
        fprintf(stderr, "FAIL: initial read=%ld data=%u errno=%d\n",
                (long)n, (unsigned int)event, error);
        goto out;
    }

    if (expect_empty(fd) < 0)
        goto out;
    puts("PASS: empty nonblocking read -> EAGAIN");

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    /* 此一秒内不要按键；没有事件应超时，而不是立即报告可读。 */
    puts("CHECK: keep KEY0 released for one second");
    ret = poll(&pfd, 1, 1000);
    if (ret < 0 && errno == EINTR && stop_requested) {
        status = 0;
        goto out;
    }
    if (ret != 0) {
        fprintf(stderr, "FAIL: expected poll timeout, ret=%d revents=0x%x\n",
                ret, (unsigned int)pfd.revents);
        goto out;
    }
    puts("PASS: empty poll -> timeout");
    puts("WAIT: press KEY0; Ctrl+C to exit");

    while (!stop_requested) {
        pfd.revents = 0;
        /* -1 表示没有超时时限；无事件时由内核睡眠等待。 */
        ret = poll(&pfd, 1, -1);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            goto out;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "FAIL: device poll error 0x%x\n",
                    (unsigned int)pfd.revents);
            goto out;
        }
        if (!(pfd.revents & POLLIN))
            continue;

        /* 未执行 read，再 poll 一次仍应可读，证明 poll 不消费事件。 */
        ret = poll(&pfd, 1, 0);
        if (ret != 1 || !(pfd.revents & POLLIN)) {
            fprintf(stderr, "FAIL: poll readiness disappeared before read\n");
            goto out;
        }

        if (first_event) {
            /*
             * 故意传入空地址测试复制失败路径，这是专项测试用法。
             * 应得到 EFAULT，并保留该事件供下一次正常 read 使用。
             */
            n = read(fd, NULL, 1);
            error = (n < 0) ? errno : 0;
            if (n != -1 || error != EFAULT) {
                fprintf(stderr, "FAIL: bad-buffer read=%ld errno=%d\n",
                        (long)n, error);
                goto out;
            }
            puts("PASS: invalid buffer -> EFAULT");
            first_event = 0;
        }

        /* 正常消费事件，检查字节数和缓冲区内容均为1。 */
        n = read(fd, &event, sizeof(event));
        if (n != 1 || event != 1) {
            fprintf(stderr, "FAIL: event read=%ld data=%u\n",
                    (long)n, (unsigned int)event);
            goto out;
        }

        /* 未再次按键时，消费后应回到无事件状态。 */
        if (expect_empty(fd) < 0)
            goto out;

        ++events;
        printf("PASS: event #%u = 1, consumed -> EAGAIN\n", events);
        puts("WAIT: next press; holding KEY0 should not repeat");
    }

    status = 0;
out:
    /* 无论正常退出还是失败都关闭文件，释放驱动的单实例占用。 */
    if (close(fd) < 0) {
        perror("close");
        status = 1;
    }
    printf("EXIT: status=%d, events=%u\n", status, events);
    return status;
}
