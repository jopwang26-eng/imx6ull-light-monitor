    /*
 * lightkey.c -- KEY0 消抖按下事件驱动。
 *
 * 目标接口：用户当前 i.MX6ULL / Linux 4.1.15 BSP。
 * 设备树 compatible：lightdemo,key0。
 * GPIO 属性：key-gpios；由设备树指定 GPIO1_IO18、GPIO_ACTIVE_LOW。
 * 字符设备：/dev/lightkey。
 *
 * 读取约定：
 *   成功 read 返回一个二进制字节 1，表示消费一次有效按下事件。
 *   没有事件时，阻塞读取等待；O_NONBLOCK 读取返回 EAGAIN。
 *   一个待处理标志；未消费期间的多次有效按下允许合并。
 *   copy_to_user 失败时保留事件，允许读取者重试。
 *
 * 首版只允许一个独立打开实例，第二次 open 返回 EBUSY。
 * poll 只报告就绪，不消费事件，仍需调用 read。
 * 模块重载时清空事件，open/release 不主动清空事件。
 *
 * 这是一份完整参考源码；实际 BSP 编译及板端验证结果仍待确认。
 */

/* 模块、platform 总线、设备树以及错误指针接口。 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/errno.h>

/* GPIO 描述符、中断、延迟工作以及毫秒到时钟节拍转换。 */
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

/* 字符设备、用户空间复制、互斥锁、原子变量和等待机制。 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/sched.h> /* Linux 4.1 等待宏所需的任务状态和 schedule 声明。 */
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/printk.h>

/* 约 30ms 的初始消抖窗口，实际执行还受调度和时钟精度影响。 */
#define LIGHTKEY_DEBOUNCE_MS 30

/* 事件状态：离线、在线无事件、在线有待处理的按下事件。 */
#define LIGHTKEY_EVENT_OFFLINE (-1)
#define LIGHTKEY_EVENT_NONE     0
#define LIGHTKEY_EVENT_PRESS    1

/*
 * 保护 GPIO 指针、稳定状态、打开状态及事件消费过程。
 * 硬中断回调不获取这把锁，它只安排延迟工作。
 */
static DEFINE_MUTEX(lightkey_lock);

/* 等待中的 read 和 poll 共用此队列。 */
static DECLARE_WAIT_QUEUE_HEAD(lightkey_wait);

/* 原子状态允许等待条件和 poll 在不获取互斥锁时安全读取。 */
static atomic_t lightkey_event = ATOMIC_INIT(LIGHTKEY_EVENT_OFFLINE);

/* 当前实现面向一个 KEY0 设备，未绑定时 GPIO 指针为 NULL。 */
static struct gpio_desc *lightkey_gpio;
static int lightkey_irq = -1;
static int lightkey_stable;
static bool lightkey_opened;
static struct delayed_work lightkey_debounce_work;

/*
 * 打开回调：限制为一个独立 open。
 * inode/file 是内核传入的文件对象，当前不需要保存私有数据。
 */
static int lightkey_open(struct inode *inode, struct file *file)
{
    int ret;

    /* 等待锁时允许被信号中断，失败则返回负错误码。 */
    ret = mutex_lock_interruptible(&lightkey_lock);
    if (ret)
        return ret;

    if (!lightkey_gpio) {
        ret = -ENODEV;
    } else if (lightkey_opened) {
        ret = -EBUSY;
    } else {
        lightkey_opened = true;
        ret = 0;
    }

    /* 打开时不清除已有的待处理事件。 */
    mutex_unlock(&lightkey_lock);
    return ret;
}

/* 最后一个引用此打开实例的文件描述符关闭时，释放打开占用。 */
static int lightkey_release(struct inode *inode, struct file *file)
{
    mutex_lock(&lightkey_lock);
    lightkey_opened = false;
    mutex_unlock(&lightkey_lock);
    return 0;
}

/*
 * 读取事件，而不是读取实时 GPIO 电平。
 * buf 为用户空间目标地址，count 为最多可接收的字节数。
 * 本接口不使用 ppos；每次成功读取返回一个字节。
 */
static ssize_t lightkey_read(struct file *file, char __user *buf,
                             size_t count, loff_t *ppos)
{
    u8 event = LIGHTKEY_EVENT_PRESS;
    int ret;

    /* 长度为零时无需等待，也不消费事件。 */
    if (count == 0)
        return 0;

    for (;;) {
        ret = mutex_lock_interruptible(&lightkey_lock);
        if (ret)
            return ret;

        /* 每次醒来都重新检查设备是否仍可用。 */
        if (!lightkey_gpio) {
            mutex_unlock(&lightkey_lock);
            return -ENODEV;
        }

        /* 有事件则保持持锁状态，跳到下面完成复制和消费。 */
        if (atomic_read(&lightkey_event) == LIGHTKEY_EVENT_PRESS)
            break;

        /* 不能持锁等待，否则消抖工作无法获取锁并产生事件。 */
        mutex_unlock(&lightkey_lock);

        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /*
         * 有事件或设备离线时结束等待。
         * 条件检查配合等待队列，避免准备睡眠期间丢失唤醒。
         * 收到信号时传播错误，交给内核系统调用/信号机制处理。
         */
        ret = wait_event_interruptible(
            lightkey_wait,
            atomic_read(&lightkey_event) != LIGHTKEY_EVENT_NONE);
        if (ret)
            return ret;
    }

    /*
     * 仍持有互斥锁，允许执行可能睡眠的用户空间复制。
     * 整个复制及消费过程与工作回调互斥，避免清掉新发布的事件。
     */
    if (copy_to_user(buf, &event, sizeof(event))) {
        ret = -EFAULT;
    } else {
        /* 只有复制成功后才消费事件；返回值为字节数 1。 */
        atomic_set(&lightkey_event, LIGHTKEY_EVENT_NONE);
        ret = sizeof(event);
    }

    mutex_unlock(&lightkey_lock);
    return ret;
}

/* Linux 4.1 的 poll 回调返回 unsigned int 类型的就绪标志。 */
static unsigned int lightkey_poll(struct file *file, poll_table *wait)
{
    int state;

    /* 先登记等待，再检查状态；此函数本身不在这里睡眠。 */
    poll_wait(file, &lightkey_wait, wait);
    state = atomic_read(&lightkey_event);

    if (state == LIGHTKEY_EVENT_OFFLINE)
        return POLLERR | POLLHUP;

    if (state == LIGHTKEY_EVENT_PRESS)
        return POLLIN | POLLRDNORM;

    /* 无事件；poll 不消费事件，实际消费由 read 完成。 */
    return 0;
}

/* 文件操作表：把用户空间操作连接到上述驱动回调。 */
static const struct file_operations lightkey_fops = {
    /* 打开期间持有模块引用，阻止普通 rmmod 卸载正在使用的代码。 */
    .owner   = THIS_MODULE,
    .open    = lightkey_open,
    .release = lightkey_release,
    .read    = lightkey_read,
    .poll    = lightkey_poll,
    .llseek  = no_llseek,
};

/* 注册后由板端现有 devtmpfs 环境生成 /dev/lightkey。 */
static struct miscdevice lightkey_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "lightkey",
    .fops  = &lightkey_fops,
};

/*
 * 延迟工作处于进程上下文，可以获取互斥锁和调用 cansleep 接口。
 * work 参数由内核传入；单设备版本直接使用本文件的共享状态。
 */
static void lightkey_debounce_handler(struct work_struct *work)
{
    int value;

    mutex_lock(&lightkey_lock);
    if (!lightkey_gpio) {
        mutex_unlock(&lightkey_lock);
        return;
    }

    /* ACTIVE_LOW 已由 GPIO 子系统转换：松开 0、按下 1。 */
    value = gpiod_get_value_cansleep(lightkey_gpio);
    if (value < 0) {
        pr_err("lightkey: debounce read failed: %d\n", value);
        mutex_unlock(&lightkey_lock);
        return;
    }
    value = !!value;

    if (value != lightkey_stable) {
        /* 松开也必须记录，为下一次识别 0 -> 1 建立起点。 */
        lightkey_stable = value;

        if (value) {
            /* 已有事件时仍保持 1，允许未消费事件合并。 */
            atomic_set(&lightkey_event, LIGHTKEY_EVENT_PRESS);
            wake_up_interruptible(&lightkey_wait);
            pr_info("lightkey: debounced press\n");
        }
    }

    mutex_unlock(&lightkey_lock);
}

/*
 * 硬中断不能睡眠：不读取 GPIO，不获取互斥锁，不执行用户复制。
 * 每个边沿重新安排约 30ms 后执行消抖检查。
 */
static irqreturn_t lightkey_irq_handler(int irq, void *dev_id)
{
    mod_delayed_work(system_wq, &lightkey_debounce_work,
                     msecs_to_jiffies(LIGHTKEY_DEBOUNCE_MS));
    return IRQ_HANDLED;
}

/* 匹配成功后，依次建立 GPIO、工作项、IRQ 和字符设备。 */
static int lightkey_probe(struct platform_device *pdev)
{
    struct gpio_desc *key;
    int irq;
    int value;
    int ret;

    /* "key" 对应设备树 key-gpios；devm 负责最终释放 GPIO。 */
    key = devm_gpiod_get(&pdev->dev, "key", GPIOD_IN);
    if (IS_ERR(key)) {
        dev_err(&pdev->dev, "get key GPIO failed: %ld\n",
                PTR_ERR(key));
        return PTR_ERR(key);
    }

    /* GPIO 编号不能直接当作 IRQ，必须经过描述符映射。 */
    irq = gpiod_to_irq(key);
    if (irq < 0) {
        dev_err(&pdev->dev, "get key IRQ failed: %d\n", irq);
        return irq;
    }

    /* 建立初始状态；加载时已经按住，不立即生成新按下事件。 */
    value = gpiod_get_value_cansleep(key);
    if (value < 0) {
        dev_err(&pdev->dev, "read initial key failed: %d\n", value);
        return value;
    }

    mutex_lock(&lightkey_lock);
    /* 旧打开实例未关闭时，也不允许重新绑定到新设备状态。 */
    if (lightkey_gpio || lightkey_opened) {
        mutex_unlock(&lightkey_lock);
        return -EBUSY;
    }

    lightkey_gpio = key;
    lightkey_irq = irq;
    lightkey_stable = !!value;
    atomic_set(&lightkey_event, LIGHTKEY_EVENT_NONE);
    mutex_unlock(&lightkey_lock);

    /* IRQ 可能在申请后立即发生，因此必须先初始化工作对象。 */
    INIT_DELAYED_WORK(&lightkey_debounce_work,
                      lightkey_debounce_handler);

    /*
     * 双边沿跟踪按下和松开；名称显示在 /proc/interrupts。
     * pdev 作为 dev_id，释放时必须传回同一指针。
     */
    ret = devm_request_irq(&pdev->dev, irq, lightkey_irq_handler,
                           IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                           "lightkey", pdev);
    if (ret) {
        dev_err(&pdev->dev, "request key IRQ failed: %d\n", ret);
        goto err_clear;
    }

    /* 每次绑定都重新申请动态次设备号。 */
    lightkey_misc.parent = &pdev->dev;
    lightkey_misc.minor = MISC_DYNAMIC_MINOR;
    ret = misc_register(&lightkey_misc);
    if (ret) {
        dev_err(&pdev->dev, "register misc failed: %d\n", ret);
        /* 先阻止新的工作入队，再跳到下面取消已有工作。 */
        devm_free_irq(&pdev->dev, irq, pdev);
        goto err_clear;
    }

    /* 版本标识方便确认加载的是完整的事件读取版本。 */
    dev_info(&pdev->dev,
             "/dev/lightkey event-v1 ready, IRQ=%d, debounce=%dms\n",
             irq, LIGHTKEY_DEBOUNCE_MS);
    return 0;

err_clear:
    /*
     * 走到这里时工作对象已初始化，且没有仍注册的本设备 IRQ。
     * 等待工作完成时不能持有工作回调也需要的 lightkey_lock。
     */
    cancel_delayed_work_sync(&lightkey_debounce_work);
    mutex_lock(&lightkey_lock);
    lightkey_gpio = NULL;
    lightkey_irq = -1;
    lightkey_stable = 0;
    atomic_set(&lightkey_event, LIGHTKEY_EVENT_OFFLINE);
    mutex_unlock(&lightkey_lock);
    wake_up_interruptible(&lightkey_wait);
    /* GPIO 在 probe 失败返回后由 devm 释放。 */
    return ret;
}

/* 用户当前 BSP 的 platform remove 回调返回 int。 */
static int lightkey_remove(struct platform_device *pdev)
{
    /* 顺序：阻止新打开 -> 停止 IRQ -> 等待工作 -> 标记离线。 */
    misc_deregister(&lightkey_misc);
    devm_free_irq(&pdev->dev, lightkey_irq, pdev);
    cancel_delayed_work_sync(&lightkey_debounce_work);

    mutex_lock(&lightkey_lock);
    lightkey_gpio = NULL;
    lightkey_irq = -1;
    lightkey_stable = 0;
    atomic_set(&lightkey_event, LIGHTKEY_EVENT_OFFLINE);
    /* lightkey_opened 留给旧打开实例的 release 清除。 */
    mutex_unlock(&lightkey_lock);

    /* 解绑场景下，旧读取者醒来后返回 ENODEV，poll 报错/挂断。 */
    wake_up_interruptible(&lightkey_wait);
    dev_info(&pdev->dev, "lightkey remove\n");
    /* 此后由 devm 释放 GPIO；IRQ 已显式撤销，不会重复释放。 */
    return 0;
}

/* 必须与现有测试设备树 lightkey-test 节点的 compatible 一致。 */
static const struct of_device_id lightkey_of_match[] = {
    { .compatible = "lightdemo,key0" },
    { }
};
MODULE_DEVICE_TABLE(of, lightkey_of_match);

/* 把设备树匹配、probe、remove 连接到 platform 总线。 */
static struct platform_driver lightkey_driver = {
    .probe  = lightkey_probe,
    .remove = lightkey_remove,
    .driver = {
        .name           = "lightkey-demo",
        .owner          = THIS_MODULE,
        .of_match_table = lightkey_of_match,
    },
};

/* 生成模块加载时的驱动注册和卸载时的驱动注销代码。 */
module_platform_driver(lightkey_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KEY0 debounced press event driver");
MODULE_VERSION("event-v1");