/*
 * i.MX6ULL 蜂鸣器字符驱动，面向项目 Linux 4.1 BSP。
 * write 一字节二进制 0/1 表示关/开；read 返回 GPIO 逻辑回读值。
 * GPIO_ACTIVE_LOW 的物理反相由 gpiod 完成，业务代码始终使用逻辑值。
 * 回读只能检查 GPIO 电平，不能代替实物声音测试。
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

/* 单板单蜂鸣器。锁同时保护描述符、打开计数及所有输出操作。 */
static DEFINE_MUTEX(beep_lock);
static struct gpio_desc *beep_gpio;
static unsigned int beep_opened;

/*
 * direction_output 有错误返回，不能使用无返回值的 set_value 冒充成功。
 * 输出后检查逻辑电平；下层错误原样传递，回读不符返回 EIO。
 * 调用者持锁。控制失败后不记录一个虚假的“已关闭”状态。
 */
static int beep_set(struct gpio_desc *gpio, unsigned char value)
{
    int ret = gpiod_direction_output(gpio, value);

    if (ret)
        return ret;
    ret = gpiod_get_value_cansleep(gpio);
    if (ret < 0)
        return ret;
    return !!ret == value ? 0 : -EIO;
}

/* 独占打开，避免两个程序竞争控制输出。文件关闭不隐式改变状态。 */
static int beep_open(struct inode *inode, struct file *file)
{
    int ret = mutex_lock_interruptible(&beep_lock);

    if (ret)
        return ret;
    if (!beep_gpio)
        ret = -ENODEV;
    else if (beep_opened)
        ret = -EBUSY;
    else {
        ret = nonseekable_open(inode, file);
        if (!ret)
            beep_opened = 1;
    }
    mutex_unlock(&beep_lock);
    return ret;
}

static int beep_release(struct inode *inode, struct file *file)
{
    mutex_lock(&beep_lock);
    beep_opened = 0;
    mutex_unlock(&beep_lock);
    return 0;
}

static ssize_t beep_write(struct file *file, const char __user *buffer,
                          size_t count, loff_t *position)
{
    unsigned char value;
    int ret;

    if (!count)
        return 0;
    if (count != 1)
        return -EINVAL;
    /* 先完整验证输入；非法数据和复制失败都不改变输出。 */
    if (copy_from_user(&value, buffer, 1))
        return -EFAULT;
    if (value > 1)
        return -EINVAL;
    ret = mutex_lock_interruptible(&beep_lock);
    if (ret)
        return ret;
    ret = beep_gpio ? beep_set(beep_gpio, value) : -ENODEV;
    mutex_unlock(&beep_lock);
    return ret ? ret : 1;
}

static ssize_t beep_read(struct file *file, char __user *buffer,
                         size_t count, loff_t *position)
{
    unsigned char value;
    int ret;

    if (!count)
        return 0;
    ret = mutex_lock_interruptible(&beep_lock);
    if (ret)
        return ret;
    if (!beep_gpio) {
        ret = -ENODEV;
        goto out;
    }
    ret = gpiod_get_value_cansleep(beep_gpio);
    if (ret < 0)
        goto out;
    value = !!ret;
    ret = copy_to_user(buffer, &value, 1) ? -EFAULT : 1;
out:
    mutex_unlock(&beep_lock);
    return ret;
}

static const struct file_operations beep_fops = {
    .owner = THIS_MODULE, /* 持有文件期间拒绝普通 rmmod。 */
    .open = beep_open,
    .release = beep_release,
    .read = beep_read,
    .write = beep_write,
    .llseek = no_llseek,
};

static struct miscdevice beep_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "lightbeep",
    .fops = &beep_fops,
};

static int beep_probe(struct platform_device *pdev)
{
    struct gpio_desc *gpio;
    int ret;

    mutex_lock(&beep_lock);
    /* 手动解绑后的旧 fd 未关闭时拒绝重新绑定，防止跨代访问。 */
    if (beep_gpio || beep_opened) {
        mutex_unlock(&beep_lock);
        return -EBUSY;
    }
    /* 申请时立即设置逻辑关闭；低有效线路对应物理高电平。 */
    gpio = devm_gpiod_get(&pdev->dev, "beep", GPIOD_OUT_LOW);
    if (IS_ERR(gpio)) {
        ret = PTR_ERR(gpio);
        dev_err(&pdev->dev, "GPIO request failed: %d\n", ret);
        goto unlock;
    }
    ret = beep_set(gpio, 0);
    if (ret)
        goto unlock;
    beep_gpio = gpio;
    mutex_unlock(&beep_lock);

    /* misc 内部也有锁，注册/注销时不能持有 beep_lock。 */
    beep_misc.parent = &pdev->dev;
    beep_misc.minor = MISC_DYNAMIC_MINOR;
    ret = misc_register(&beep_misc);
    if (ret) {
        mutex_lock(&beep_lock);
        beep_gpio = NULL;
        mutex_unlock(&beep_lock);
        return ret;
    }
    dev_info(&pdev->dev, "beep-v1.1 ready: /dev/lightbeep default=off\n");
    return 0;
unlock:
    mutex_unlock(&beep_lock);
    dev_err(&pdev->dev, "GPIO initialization failed: %d\n", ret);
    return ret;
}

static int beep_remove(struct platform_device *pdev)
{
    int ret;

    misc_deregister(&beep_misc);
    mutex_lock(&beep_lock);
    ret = beep_set(beep_gpio, 0);
    /* 先使旧 fd 失效，返回后 devm 才释放 GPIO。 */
    beep_gpio = NULL;
    mutex_unlock(&beep_lock);
    if (ret)
        dev_err(&pdev->dev, "remove: cannot confirm off: %d\n", ret);
    dev_info(&pdev->dev, "lightbeep remove, off_result=%d\n", ret);
    /* remove 返回错误不会阻止驱动核心解绑，故用日志报告关闭失败。 */
    return 0;
}

static void beep_shutdown(struct platform_device *pdev)
{
    int ret = 0;

    mutex_lock(&beep_lock);
    if (beep_gpio)
        ret = beep_set(beep_gpio, 0);
    mutex_unlock(&beep_lock);
    if (ret)
        dev_err(&pdev->dev, "shutdown: cannot confirm off: %d\n", ret);
}

static const struct of_device_id beep_match[] = {
    { .compatible = "lightdemo,beep" },
    { }
};
MODULE_DEVICE_TABLE(of, beep_match);

static struct platform_driver beep_driver = {
    .probe = beep_probe,
    .remove = beep_remove,
    .shutdown = beep_shutdown,
    .driver = {
        .name = "lightbeep",
        .of_match_table = beep_match,
    },
};
module_platform_driver(beep_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Light monitor GPIO buzzer character driver");
MODULE_VERSION("beep-v1.1");
