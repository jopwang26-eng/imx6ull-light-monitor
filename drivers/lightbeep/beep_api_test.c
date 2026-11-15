/* 板端接口边界测试：始终以关闭状态执行，不进行声音测试。 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;

static void expect(const char *name, int passed)
{
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
        ++failures;
}

int main(void)
{
    unsigned char invalid = 2, pair[2] = {0, 1}, state = 255;
    unsigned char off = 0;
    int fd = open("/dev/lightbeep", O_RDWR), second;
    long ret;

    if (fd < 0) {
        perror("open lightbeep");
        return 1;
    }
    /* 先确认可以关闭，失败则停止，避免在未知输出状态继续测试。 */
    if (write(fd, &off, 1) != 1) {
        perror("initial off");
        close(fd);
        return 1;
    }
    errno = 0;
    second = open("/dev/lightbeep", O_RDWR);
    expect("exclusive open EBUSY", second == -1 && errno == EBUSY);
    if (second >= 0)
        close(second);
    errno = 0;
    ret = write(fd, &invalid, 1);
    expect("invalid value EINVAL", ret == -1 && errno == EINVAL);
    errno = 0;
    ret = write(fd, pair, 2);
    expect("invalid length EINVAL", ret == -1 && errno == EINVAL);
    /* 使用原始系统调用传入空地址，避免用户态库的静态参数诊断。 */
    errno = 0;
    ret = syscall(SYS_write, fd, (void *)0, 1);
    expect("bad write pointer EFAULT", ret == -1 && errno == EFAULT);
    errno = 0;
    ret = syscall(SYS_read, fd, (void *)0, 1);
    expect("bad read pointer EFAULT", ret == -1 && errno == EFAULT);
    expect("zero write", syscall(SYS_write, fd, (void *)0, 0) == 0);
    expect("zero read", syscall(SYS_read, fd, (void *)0, 0) == 0);
    ret = read(fd, &state, 1);
    expect("invalid requests kept GPIO off", ret == 1 && state == 0);
    expect("close", close(fd) == 0);
    fd = open("/dev/lightbeep", O_RDWR);
    expect("reopen after close", fd >= 0);
    if (fd >= 0)
        close(fd);
    printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
