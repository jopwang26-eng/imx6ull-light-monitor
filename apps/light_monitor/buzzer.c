/* 蜂鸣器适配：写入0/1并校验GPIO逻辑回读，不将它解释为声音检测。 */
#include "buzzer.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int lm_buzzer_close(struct lm_buzzer *buzzer)
{
    int error = 0;
    if (buzzer->fd >= 0 && close(buzzer->fd) < 0)
        error = errno;
    buzzer->fd = -1;
    return error;
}

int lm_buzzer_set(struct lm_buzzer *buzzer, unsigned char value)
{
    struct stat status;
    unsigned char actual;
    ssize_t count;
    int error;
    if (buzzer->fd < 0) {
        /* O_RDWR取得读写权限，NONBLOCK避免意外等待其他类型的测试节点。 */
        buzzer->fd = open(buzzer->path, O_RDWR | O_NONBLOCK);
        if (buzzer->fd < 0)
            return errno;
    }
    if (fstat(buzzer->fd, &status) < 0) {
        error = errno;
        goto failed;
    }
    /* 普通文件仅供原生夹具使用；字符设备不依赖文件偏移。 */
    if (S_ISREG(status.st_mode) && lseek(buzzer->fd, 0, SEEK_SET) < 0) {
        error = errno;
        goto failed;
    }
    count = write(buzzer->fd, &value, 1);
    if (count != 1) {
        error = count < 0 ? errno : EIO;
        goto failed;
    }
    if (S_ISREG(status.st_mode) && lseek(buzzer->fd, 0, SEEK_SET) < 0) {
        error = errno;
        goto failed;
    }
    count = read(buzzer->fd, &actual, 1);
    if (count != 1 || actual != value) {
        error = count < 0 ? errno : EIO;
        goto failed;
    }
    return 0;
failed:
    lm_buzzer_close(buzzer);
    return error;
}
