#ifndef LIGHT_MONITOR_LOGGER_H
#define LIGHT_MONITOR_LOGGER_H
#include <pthread.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

#define LM_LOG_CAPACITY 64
#define LM_LOG_DEFAULT_LIMIT (10U * 1024U * 1024U)

/* 一次成功采样形成一条记录；序号和运行时间在每次启动时重新开始。 */
struct lm_record {
    uint64_t sequence, elapsed_ms;
    unsigned int raw;
    int alarm; /* -1未启用阈值、0解除、1报警，是业务判断而非实物声音。 */
};
struct lm_log_stats {
    uint64_t saved, dropped, failures, uncertain;
    unsigned int queued, in_flight;
    int failed, last_errno;
};
struct lm_logger {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t thread;
    struct lm_record queue[LM_LOG_CAPACITY];
    unsigned int head, count;
    int stopping;
    struct lm_log_stats stats;
    char directory[PATH_MAX];
    unsigned int limit;
    /* 下列字段只归日志线程所有，不在锁外给其他线程读取。 */
    int fd, lock_fd, tainted;
    dev_t tainted_dev;
    ino_t tainted_ino;
};
int lm_logger_start(struct lm_logger *logger, const char *directory,
                    unsigned int limit);
void lm_logger_submit(struct lm_logger *logger, const struct lm_record *record);
void lm_logger_snapshot(struct lm_logger *logger, struct lm_log_stats *out);
int lm_logger_stop(struct lm_logger *logger);
#endif
