/*
 * AP3216C ALS-v1：Linux 4.1 BSP 的 I2C 字符设备驱动。
 *
 * /dev/lightap3216c 每次 read 返回两个字节：低位在前、高位在后。
 * 该数值是光照原始值，不做 lux 换算，不承诺每次都是新转换结果。
 * 初始化、寄存器读取、解绑由同一互斥锁串行化。
 * 本板只有一个目标传感器；旧文件未关闭时不允许重新绑定，
 * 防止解绑前打开的文件描述符意外访问下一轮设备。
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/errno.h>

#define LIGHTAP_SYS_CONFIG 0x00
#define LIGHTAP_ALS_LOW    0x0c
#define LIGHTAP_ALS_HIGH   0x0d
#define LIGHTAP_ALS_CONFIG 0x10
#define LIGHTAP_POWER_DOWN 0x00
#define LIGHTAP_ALS_ON     0x01
#define LIGHTAP_SW_RESET   0x04
#define LIGHTAP_FRAME_SIZE 2

/* 这些状态的全部读写都由 lightap_lock 保护。 */
static DEFINE_MUTEX(lightap_lock);
static struct i2c_client *lightap_client;
static unsigned int lightap_open_count;

/*
 * 一次寄存器读取包含“写地址 + repeated-start 读一个字节”。
 * 必须完成两条消息才算成功；保留负错误码，短传输转换为 EIO。
 * 调用者负责持锁，避免低/高字节读取之间插入本驱动的其他读取。
 */
static int lightap_read_reg(struct i2c_client *client, u8 reg, u8 *value)
{
    struct i2c_msg messages[2] = {
        { .addr = client->addr, .flags = 0, .len = 1, .buf = &reg },
        { .addr = client->addr, .flags = I2C_M_RD, .len = 1, .buf = value },
    };
    int ret = i2c_transfer(client->adapter, messages, 2);

    return ret == 2 ? 0 : (ret < 0 ? ret : -EIO);
}

/* 将寄存器地址和数据作为同一条 I2C 消息发送。 */
static int lightap_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
    u8 bytes[2] = { reg, value };
    struct i2c_msg message = {
        .addr = client->addr, .flags = 0, .len = sizeof(bytes), .buf = bytes,
    };
    int ret = i2c_transfer(client->adapter, &message, 1);

    return ret == 1 ? 0 : (ret < 0 ? ret : -EIO);
}

/*
 * 复位后配置 ALS 连续转换。
 * 资料给出的复位时间为10ms，沿用实际 BSP 更宽裕的50ms等待。
 * ALS典型转换100ms，首版等待150ms再发布字符设备。
 */
static int lightap_start(struct i2c_client *client)
{
    u8 mode;
    int ret;

    ret = lightap_write_reg(client, LIGHTAP_SYS_CONFIG, LIGHTAP_SW_RESET);
    if (ret)
        return ret;
    msleep(50);

    /* 明确选择默认量程，不依赖前一个驱动留下的增益设置。 */
    ret = lightap_write_reg(client, LIGHTAP_ALS_CONFIG, 0x00);
    if (ret)
        return ret;
    ret = lightap_write_reg(client, LIGHTAP_SYS_CONFIG, LIGHTAP_ALS_ON);
    if (ret)
        return ret;
    msleep(150);

    /* 回读模式确认配置生效；不把它当作芯片型号鉴别。 */
    ret = lightap_read_reg(client, LIGHTAP_SYS_CONFIG, &mode);
    if (ret)
        return ret;
    return (mode & 0x07) == LIGHTAP_ALS_ON ? 0 : -EIO;
}

/* 停止转换失败时保留诊断；设备资源仍必须完成注销。 */
static void lightap_stop(struct i2c_client *client)
{
    int ret = lightap_write_reg(client, LIGHTAP_SYS_CONFIG, LIGHTAP_POWER_DOWN);

    if (ret)
        dev_warn(&client->dev, "power-down failed: %d\n", ret);
}

/* 打开时确认设备在线，并记录可能跨越手动解绑的文件实例。 */
static int lightap_open(struct inode *inode, struct file *file)
{
    int ret = mutex_lock_interruptible(&lightap_lock);

    if (ret)
        return ret;
    if (!lightap_client) {
        ret = -ENODEV;
    } else if (!(file->f_mode & FMODE_READ)) {
        ret = -EACCES;
    } else {
        ret = nonseekable_open(inode, file);
        if (!ret)
            ++lightap_open_count;
    }
    mutex_unlock(&lightap_lock);
    return ret;
}

/* 最后一个引用该打开实例的描述符关闭时，解除占用计数。 */
static int lightap_release(struct inode *inode, struct file *file)
{
    mutex_lock(&lightap_lock);
    if (lightap_open_count)
        --lightap_open_count;
    mutex_unlock(&lightap_lock);
    return 0;
}

/*
 * 只在两个寄存器均读取成功后复制完整样本。
 * 小于两字节的非零缓冲区返回 EINVAL；不拆分样本，不使用文件偏移。
 * 读失败不返回旧数据，O_NONBLOCK 不表示等待下一次ADC转换。
 */
static ssize_t lightap_read(struct file *file, char __user *buffer,
                           size_t count, loff_t *position)
{
    u8 raw[LIGHTAP_FRAME_SIZE];
    int ret;

    if (!count)
        return 0;
    if (count < sizeof(raw))
        return -EINVAL;
    ret = mutex_lock_interruptible(&lightap_lock);
    if (ret)
        return ret;
    if (!lightap_client) {
        ret = -ENODEV;
        goto out;
    }

    /* 先读低位，使芯片锁存对应高位，避免拼接不同转换周期。 */
    ret = lightap_read_reg(lightap_client, LIGHTAP_ALS_LOW, &raw[0]);
    if (ret)
        goto out;
    ret = lightap_read_reg(lightap_client, LIGHTAP_ALS_HIGH, &raw[1]);
    if (ret)
        goto out;

    /* raw 本身就是定义好的小端两字节协议，与主机CPU字节序无关。 */
    ret = copy_to_user(buffer, raw, sizeof(raw)) ? -EFAULT : sizeof(raw);
out:
    mutex_unlock(&lightap_lock);
    return ret;
}

static const struct file_operations lightap_fops = {
    .owner = THIS_MODULE, /* 打开文件期间阻止普通模块卸载。 */
    .open = lightap_open,
    .release = lightap_release,
    .read = lightap_read,
    .llseek = no_llseek,
};

static struct miscdevice lightap_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "lightap3216c",
    .fops = &lightap_fops,
};

/* 当前 BSP 的 I2C probe 原型包含 client、id 两个参数。 */
static int lightap_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -EOPNOTSUPP;
    if (client->flags & I2C_CLIENT_TEN)
        return -EINVAL;

    mutex_lock(&lightap_lock);
    if (lightap_client || lightap_open_count) {
        mutex_unlock(&lightap_lock);
        return -EBUSY;
    }
    ret = lightap_start(client);
    if (ret) {
        dev_err(&client->dev, "ALS initialization failed: %d\n", ret);
        lightap_stop(client);
        mutex_unlock(&lightap_lock);
        return ret;
    }
    lightap_client = client;
    mutex_unlock(&lightap_lock);

    /* 发布节点之前初始化已完成；每次重新绑定都申请动态次设备号。 */
    lightap_misc.parent = &client->dev;
    lightap_misc.minor = MISC_DYNAMIC_MINOR;
    ret = misc_register(&lightap_misc);
    if (ret) {
        mutex_lock(&lightap_lock);
        lightap_stop(client);
        lightap_client = NULL;
        mutex_unlock(&lightap_lock);
        dev_err(&client->dev, "misc registration failed: %d\n", ret);
        return ret;
    }
    dev_info(&client->dev,
             "als-v1 ready: bus=%d addr=0x%02x node=/dev/lightap3216c\n",
             client->adapter->nr, client->addr);
    return 0;
}

/*
 * misc_deregister 必须放在互斥锁外，避免与 misc_open 的锁顺序冲突。
 * 获取锁后等待在途读取结束，再关闭转换并清空指针。
 * 手动解绑后旧文件 read 返回 ENODEV；关闭旧文件后才允许重新绑定。
 */
static int lightap_remove(struct i2c_client *client)
{
    misc_deregister(&lightap_misc);
    mutex_lock(&lightap_lock);
    lightap_stop(client);
    lightap_client = NULL;
    mutex_unlock(&lightap_lock);
    dev_info(&client->dev, "lightap3216c remove\n");
    return 0;
}

/* 沿用已确认的设备树，不需要修改DTB或关闭共享的I2C控制器。 */
static const struct of_device_id lightap_of_match[] = {
    { .compatible = "ap3216c" },
    { }
};
MODULE_DEVICE_TABLE(of, lightap_of_match);

static const struct i2c_device_id lightap_ids[] = {
    { "ap3216c", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, lightap_ids);

static struct i2c_driver lightap_driver = {
    .driver = {
        .name = "lightap3216c",
        .owner = THIS_MODULE,
        .of_match_table = lightap_of_match,
    },
    .probe = lightap_probe,
    .remove = lightap_remove,
    .id_table = lightap_ids,
};
module_i2c_driver(lightap_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AP3216C raw ALS character driver");
MODULE_VERSION("als-v1");
