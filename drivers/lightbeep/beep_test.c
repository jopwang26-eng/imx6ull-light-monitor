/* 蜂鸣器板端工具：on/off 控制，status 回读 GPIO 逻辑电平。 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *device = "/dev/lightbeep";
    unsigned char value, actual;
    int fd, result = 1, status_only;

    /* 可选设备路径便于通过普通文件验证公开字节协议。 */
    if ((argc != 2 && argc != 3) ||
        (strcmp(argv[1], "on") && strcmp(argv[1], "off") &&
         strcmp(argv[1], "status"))) {
        fprintf(stderr, "Usage: beep_test on|off|status [DEVICE]\n");
        return 2;
    }
    if (argc == 3)
        device = argv[2];
    status_only = !strcmp(argv[1], "status");
    value = !strcmp(argv[1], "on");
    fd = open(device, status_only ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        perror("open buzzer");
        return 1;
    }
    if (!status_only && write(fd, &value, 1) != 1) {
        perror("write buzzer (or short write)");
        goto out;
    }
    /* 字符设备忽略偏移；普通文件夹具需移回文件头再检查协议值。 */
    if (lseek(fd, 0, SEEK_SET) < 0) {
        /* 目标字符设备不可 seek，这是预期行为。 */
    }
    if (read(fd, &actual, 1) != 1) {
        fprintf(stderr, "buzzer read failed or short frame\n");
        goto out;
    }
    if (actual > 1 || (!status_only && actual != value)) {
        fprintf(stderr, "buzzer readback mismatch: %u\n", actual);
        goto out;
    }
    printf("GPIO_LOGICAL=%u (%s)\n", actual, actual ? "on" : "off");
    result = 0;
out:
    if (close(fd) < 0) {
        perror("close buzzer");
        result = 1;
    }
    return result;
}
