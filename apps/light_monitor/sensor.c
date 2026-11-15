/* 设备适配层：封装 /dev/lightap3216c 的两字节小端协议。 */
#include "light_monitor.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int lm_sensor_read(const char *device, unsigned int *raw)
{
    unsigned char bytes[2];
    ssize_t count;
    int error = 0;
    /*
     * 每周期重新打开，读完即关闭，避免失效旧fd阻挡驱动重新绑定。
     * open参数为路径及只读/非阻塞标志；失败返回-1并设置errno。
     * O_NONBLOCK避免命名管道夹具打开等待；不会中断驱动内部I2C传输。
     */
    int fd = open(device, O_RDONLY | O_NONBLOCK);

    if (fd < 0)
        return errno;
    /* read参数为fd、目标缓冲区、容量；只接受完整的两字节样本。 */
    count = read(fd, bytes, sizeof(bytes));
    if (count < 0)
        error = errno;
    else if (count != (ssize_t)sizeof(bytes))
        error = EIO;
    /* close即使失败也不能丢失先前读取错误；不对close盲目重试。 */
    if (close(fd) < 0 && !error)
        error = errno;
    if (error)
        return error;
    /* 位或与左移是运算符，不是函数调用；与CPU字节序无关。 */
    *raw = (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8);
    return 0;
}
