/* 仅供原生进程测试LD_PRELOAD使用，ARM正式程序不链接此文件。
 * 在一个真实fdatasync调用前阻塞，模拟存储延迟；删除gate文件即恢复。
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

int fdatasync(int fd)
{
    const char *gate = getenv("LM_TEST_SYNC_GATE");
    int (*real_sync)(int) = dlsym(RTLD_NEXT, "fdatasync");
    while (gate && access(gate, F_OK) == 0)
        usleep(10000);
    return real_sync(fd);
}
