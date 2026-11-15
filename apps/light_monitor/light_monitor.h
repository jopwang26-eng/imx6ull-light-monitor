#ifndef LIGHT_MONITOR_H
#define LIGHT_MONITOR_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "logger.h"
#include "buzzer.h"

enum lm_action { LM_START, LM_STOP, LM_THRESHOLD, LM_INTERVAL, LM_TOGGLE };

/* 最新状态快照不是历史队列；失败时 raw 无效，不能当作成功样本。 */
struct lm_snapshot {
    uint64_t attempts, successes, errors, missed, elapsed_ms;
    unsigned int raw;
    int valid, last_errno, fatal_errno;
    unsigned int interval_ms, low, high;
    int running, threshold_enabled, alarm;
    int buzzer_known, buzzer_on, control_errno, key_errno, key_enabled;
    uint64_t control_errors, key_errors, superseded;
    struct lm_log_stats log;
};

/* device固定不变；运行配置、状态、蜂鸣器和generation均由lock保护。 */
struct lm_sampler {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t thread;
    const char *device;
    uint64_t generation;
    uint64_t started_ms;
    int stopping;
    struct lm_snapshot state;
    struct lm_logger *logger;
    struct lm_buzzer buzzer;
};

/* 设备适配：成功返回0并写raw，失败返回正errno且不修改raw。 */
int lm_sensor_read(const char *device, unsigned int *raw);
/* 初始化同步资源并创建线程；失败返回正错误码并回收已申请资源。 */
int lm_sampler_start(struct lm_sampler *sampler, const char *device,
                     unsigned int interval_ms, struct lm_logger *logger,
                     const char *buzzer_path);
void lm_sampler_snapshot(struct lm_sampler *sampler, struct lm_snapshot *out);
/* 请求停止并唤醒周期等待，join后销毁资源，返回join结果。 */
int lm_sampler_stop(struct lm_sampler *sampler);
/* 命令/KEY0共用原子业务入口，错误表示控制未确认，配置可能已应用。 */
int lm_sampler_control(struct lm_sampler *sampler, enum lm_action action,
                        unsigned int first, unsigned int second);
void lm_sampler_key_status(struct lm_sampler *sampler, int enabled, int error);
/* 解析并执行业务命令，传输层仍由main负责。 */
void lm_command(const char *line, struct lm_sampler *sampler, char *reply, size_t size);
const char *lm_alarm_name(const struct lm_snapshot *state);

#endif
