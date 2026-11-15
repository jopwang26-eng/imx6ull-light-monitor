/*
 * 独立板端验证模块，正式lightap3216c.c保持原样。
 * 构建时把正式源码复制为base.c，通过宏仅替换它调用i2c_transfer的入口。
 * 正常路径仍调用真实适配器；故障路径在ALS低字节读取处发送0x1f只读事务。
 * 它测试真实控制器错误传播，不模拟芯片断线，不向其他地址写寄存器。
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/errno.h>

static int nack_transfer(struct i2c_adapter *adapter,
                         struct i2c_msg *messages, int number);

/* base.c内的函数、锁、字符接口和错误处理全部保持正式源实现。 */
#define i2c_transfer nack_transfer
#include "base.c"
#undef i2c_transfer

static bool nack_enabled;

/*
 * sysfs写入参数时与正式read使用同一把锁，保证一个read中途不切换模式。
 * param_set_bool解析Y/N或0/1，成功0、失败负errno；不改变适配器配置。
 */
static int set_nack(const char *value, const struct kernel_param *parameter)
{
    int result;
    mutex_lock(&lightap_lock);
    result = param_set_bool(value, parameter);
    mutex_unlock(&lightap_lock);
    return result;
}

static const struct kernel_param_ops nack_ops = {
    .set = set_nack,
    .get = param_get_bool,
};
module_param_cb(nack_enabled, &nack_ops, &nack_enabled, 0600);
MODULE_PARM_DESC(nack_enabled, "Test only: replace ALS read with 0x1f read-only transfer");

/*
 * adapter/messages/number与内核i2c_transfer参数一致。
 * 调用来自已持lightap_lock的正式源码。只匹配总线0、设备0x1e的ALS低字节读取，
 * 初始化、停止转换及正常模式均走原事务。负硬件返回值原样返回给正式read路径。
 */
static int nack_transfer(struct i2c_adapter *adapter,
                         struct i2c_msg *messages, int number)
{
    if (nack_enabled && adapter->nr == 0 && number == 2 &&
        messages[0].addr == 0x1e && messages[0].flags == 0 &&
        messages[0].len == 1 && messages[0].buf[0] == LIGHTAP_ALS_LOW &&
        messages[1].flags == I2C_M_RD && messages[1].len == 1) {
        u8 ignored = 0;
        struct i2c_msg request = {
            .addr = 0x1f, .flags = I2C_M_RD, .len = 1, .buf = &ignored,
        };
        int result = i2c_transfer(adapter, &request, 1);

        pr_info("LIGHTAP_NACK_TEST bus=0 addr=0x1f read_only=1 hardware_ret=%d\n", result);
        /* 意外应答意味着前提不成立；用EPROTO明确拒绝，不伪装成NACK。 */
        return result < 0 ? result : -EPROTO;
    }
    return i2c_transfer(adapter, messages, number);
}
