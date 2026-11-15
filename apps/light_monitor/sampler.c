/* 采样、报警与统一业务状态；I2C读取在业务锁之外执行。 */
#define _POSIX_C_SOURCE 200809L
#include "light_monitor.h"
#include <errno.h>
#include <string.h>
#include <time.h>

static int monotonic_ms(uint64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0)
        return errno;
    *value = (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
    return 0;
}

/* 调用者持业务锁，STOP关闭与采样触发ON不能交错越过命令返回边界。 */
static int apply_buzzer(struct lm_sampler *sampler)
{
    int error = lm_buzzer_set(&sampler->buzzer, (unsigned char)sampler->state.alarm);
    sampler->state.control_errno = error;
    sampler->state.buzzer_known = !error;
    if (error)
        ++sampler->state.control_errors;
    else
        sampler->state.buzzer_on = sampler->state.alarm;
    return error;
}

static void *sample_loop(void *argument)
{
    struct lm_sampler *sampler = argument;
    uint64_t next = sampler->started_ms, now, missed, generation, scheduled_generation = 0;
    struct timespec deadline;
    struct lm_record record;
    unsigned int raw;
    int error, read_error, wait_error;

    pthread_mutex_lock(&sampler->lock);
    while (!sampler->stopping) {
        error = monotonic_ms(&now);
        if (error)
            goto fatal;
        if (scheduled_generation != sampler->generation) {
            /* START/周期/阈值变化重新定时；暂停期间不累计采样遗漏。 */
            next = now;
            scheduled_generation = sampler->generation;
        }
        if (!sampler->state.running) {
            /* 暂停仍每秒尝试关闭，让上次关闭失败后可以自行恢复。 */
            sampler->state.alarm = 0;
            apply_buzzer(sampler);
            deadline.tv_sec = (time_t)(now / 1000) + 1;
            deadline.tv_nsec = (long)(now % 1000) * 1000000L;
            wait_error = pthread_cond_timedwait(&sampler->changed, &sampler->lock, &deadline);
            if (wait_error && wait_error != ETIMEDOUT) {
                error = wait_error;
                goto fatal;
            }
            continue;
        }
        if (now < next) {
            deadline.tv_sec = (time_t)(next / 1000);
            deadline.tv_nsec = (long)(next % 1000) * 1000000L;
            wait_error = pthread_cond_timedwait(&sampler->changed, &sampler->lock, &deadline);
            if (wait_error && wait_error != ETIMEDOUT) {
                error = wait_error;
                goto fatal;
            }
            continue;
        }
        generation = sampler->generation;
        pthread_mutex_unlock(&sampler->lock);
        raw = 0;
        read_error = lm_sensor_read(sampler->device, &raw);
        pthread_mutex_lock(&sampler->lock);
        /* I2C读取期间若状态已变，旧结果作废，尤其不能在STOP后重新报警。 */
        if (sampler->stopping || generation != sampler->generation) {
            ++sampler->state.superseded;
            continue;
        }
        ++sampler->state.attempts;
        sampler->state.valid = !read_error;
        sampler->state.last_errno = read_error;
        sampler->state.raw = read_error ? 0 : raw;
        if (read_error) {
            ++sampler->state.errors;
            sampler->state.alarm = 0;
        } else {
            ++sampler->state.successes;
            if (!sampler->state.threshold_enabled)
                sampler->state.alarm = 0;
            else if (raw < sampler->state.low)
                sampler->state.alarm = 1;
            else if (raw > sampler->state.high)
                sampler->state.alarm = 0;
            /* 中间区间和恰等于上下限时保留原报警判断。 */
        }
        apply_buzzer(sampler);
        error = monotonic_ms(&now);
        if (error)
            goto fatal;
        sampler->state.elapsed_ms = now - sampler->started_ms;
        if (sampler->state.valid) {
            record.sequence = sampler->state.successes;
            record.elapsed_ms = sampler->state.elapsed_ms;
            record.raw = raw;
            record.alarm = sampler->state.threshold_enabled ? sampler->state.alarm : -1;
            /* 不嵌套持有日志锁；STOP可与已形成记录入队并行，该记录属STOP之前。 */
            pthread_mutex_unlock(&sampler->lock);
            lm_logger_submit(sampler->logger, &record);
            pthread_mutex_lock(&sampler->lock);
        }
        if (generation != sampler->generation)
            continue;
        next += sampler->state.interval_ms;
        missed = now >= next ? (now - next) / sampler->state.interval_ms + 1 : 0;
        sampler->state.missed += missed;
        next += missed * sampler->state.interval_ms;
    }
    goto finish;
fatal:
    sampler->state.fatal_errno = error;
    sampler->state.valid = 0;
    sampler->state.raw = 0;
    sampler->stopping = 1;
finish:
    sampler->state.running = 0;
    sampler->state.alarm = 0;
    apply_buzzer(sampler); /* 信号、EOF、内部故障都经过同一关闭路径。 */
    error = lm_buzzer_close(&sampler->buzzer);
    if (error) {
        sampler->state.control_errno = error;
        sampler->state.buzzer_known = 0;
        ++sampler->state.control_errors;
    }
    pthread_mutex_unlock(&sampler->lock);
    return NULL;
}

int lm_sampler_start(struct lm_sampler *sampler, const char *device,
                     unsigned int interval_ms, struct lm_logger *logger,
                     const char *buzzer_path)
{
    pthread_condattr_t attribute;
    int error;
    memset(sampler, 0, sizeof(*sampler));
    sampler->device = device;
    sampler->state.interval_ms = interval_ms;
    sampler->state.running = 1;
    sampler->logger = logger;
    sampler->buzzer.path = buzzer_path;
    sampler->buzzer.fd = -1;
    error = monotonic_ms(&sampler->started_ms);
    if (error)
        return error;
    error = pthread_mutex_init(&sampler->lock, NULL);
    if (error)
        return error;
    error = pthread_condattr_init(&attribute);
    if (error)
        goto destroy_lock;
    error = pthread_condattr_setclock(&attribute, CLOCK_MONOTONIC);
    if (!error)
        error = pthread_cond_init(&sampler->changed, &attribute);
    pthread_condattr_destroy(&attribute);
    if (error)
        goto destroy_lock;
    /* 创建线程前没有并发访问，先关闭；失败保留采集和串口控制能力。 */
    apply_buzzer(sampler);
    error = pthread_create(&sampler->thread, NULL, sample_loop, sampler);
    if (!error)
        return 0;
    lm_buzzer_close(&sampler->buzzer);
    pthread_cond_destroy(&sampler->changed);
destroy_lock:
    pthread_mutex_destroy(&sampler->lock);
    return error;
}

int lm_sampler_control(struct lm_sampler *sampler, enum lm_action action,
                        unsigned int first, unsigned int second)
{
    int error = 0, was_running;
    /* 业务层也校验参数，未来入口不能绕过解析直接提交非法值。 */
    if ((action == LM_INTERVAL && (first < 100 || first > 60000)) ||
        (action == LM_THRESHOLD && (first >= second || second > 65535)))
        return EINVAL;
    pthread_mutex_lock(&sampler->lock);
    /* 停止清理开始后拒绝新配置，避免重新打开已关闭的蜂鸣器。 */
    if (sampler->stopping) {
        pthread_mutex_unlock(&sampler->lock);
        return ECANCELED;
    }
    was_running = sampler->state.running;
    switch (action) {
    case LM_TOGGLE: sampler->state.running = !was_running; break;
    case LM_START: sampler->state.running = 1; break;
    case LM_STOP: sampler->state.running = 0; break;
    case LM_INTERVAL: sampler->state.interval_ms = first; break;
    case LM_THRESHOLD:
        sampler->state.threshold_enabled = 1;
        sampler->state.low = first;
        sampler->state.high = second;
        sampler->state.alarm = 0;
        break;
    default: error = EINVAL; break;
    }
    if (!error) {
        if (was_running != sampler->state.running || action == LM_INTERVAL ||
            action == LM_THRESHOLD) {
            ++sampler->generation;
            sampler->state.valid = 0;
            sampler->state.raw = 0;
            sampler->state.last_errno = 0;
        }
        if (!sampler->state.running || action == LM_THRESHOLD) {
            sampler->state.alarm = 0;
            error = apply_buzzer(sampler);
        }
        pthread_cond_broadcast(&sampler->changed);
    }
    pthread_mutex_unlock(&sampler->lock);
    return error;
}

void lm_sampler_key_status(struct lm_sampler *sampler, int enabled, int error)
{
    pthread_mutex_lock(&sampler->lock);
    sampler->state.key_enabled = enabled;
    sampler->state.key_errno = error;
    if (error)
        ++sampler->state.key_errors;
    pthread_mutex_unlock(&sampler->lock);
}

void lm_sampler_snapshot(struct lm_sampler *sampler, struct lm_snapshot *out)
{
    pthread_mutex_lock(&sampler->lock);
    *out = sampler->state;
    pthread_mutex_unlock(&sampler->lock);
    lm_logger_snapshot(sampler->logger, &out->log);
}

int lm_sampler_stop(struct lm_sampler *sampler)
{
    int error;
    pthread_mutex_lock(&sampler->lock);
    sampler->stopping = 1;
    ++sampler->generation;
    pthread_cond_broadcast(&sampler->changed);
    pthread_mutex_unlock(&sampler->lock);
    error = pthread_join(sampler->thread, NULL);
    if (!error) {
        pthread_cond_destroy(&sampler->changed);
        pthread_mutex_destroy(&sampler->lock);
    }
    return error;
}
