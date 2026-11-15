/*
 * 同一个字符设备fd依次验证正常、硬件错误、恢复。
 * 故障时以哨兵检查驱动没有复制伪造数据；只接受BSP无ACK对应的errno=EIO。
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *parameter = "/sys/module/lightap_nack_test/parameters/nack_enabled";

/* 每次打开sysfs参数并写入一字节；0成功，-1失败。close失败也不能忽略。 */
static int mode(char value)
{
    int fd = open(parameter, O_WRONLY);
    ssize_t count;
    int saved;
    if (fd < 0) return -1;
    count = write(fd, &value, 1);
    saved = errno;
    if (close(fd) < 0) return -1;
    if (count != 1) { errno = count < 0 ? saved : EIO; return -1; }
    return 0;
}

/* read成功必须为两字节；故障必须-1/EIO且哨兵不变。返回0表示本阶段符合预期。 */
static int sample(int fd, const char *stage, int failure)
{
    unsigned char bytes[2] = {0xa5, 0x5a};
    ssize_t count;
    int saved;
    errno = 0;
    count = read(fd, bytes, sizeof(bytes));
    saved = errno;
    printf("DIRECT stage=%s read_ret=%ld errno=%d bytes=%02x,%02x\n",
           stage, (long)count, saved, bytes[0], bytes[1]);
    if (failure)
        return count == -1 && saved == EIO && bytes[0] == 0xa5 && bytes[1] == 0x5a ? 0 : -1;
    return count == 2 ? 0 : -1;
}

int main(void)
{
    int fd, result = 1;
    setvbuf(stdout, NULL, _IOLBF, 0);
    fd = open("/dev/lightap3216c", O_RDONLY);
    if (fd < 0) { perror("open sensor"); return 1; }
    if (mode('0') || sample(fd, "normal", 0)) goto out;
    if (mode('1') || sample(fd, "nack", 1)) goto out;
    if (mode('0') || sample(fd, "recovered", 0)) goto out;
    result = 0;
out:
    /* 任一断言失败仍先关闭测试模式；退出码保留失败，供Shell决定是否继续。 */
    if (mode('0')) { perror("disable nack"); result = 1; }
    if (close(fd)) { perror("close sensor"); result = 1; }
    printf("DIRECT_RESULT=%d\n", result);
    return result;
}
