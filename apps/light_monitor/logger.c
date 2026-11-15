/*
 * CSV唯一写入者。采集端只在短临界区内复制记录，从不等待存储I/O。
 * 满队列丢弃新记录；存储失败时丢弃待写记录，5秒后重新尝试打开。
 */
#define _POSIX_C_SOURCE 200809L
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char header[] = "sequence,elapsed_ms,als_raw,alarm\n";

static int log_path(struct lm_logger *logger, unsigned int index, char *path)
{
    int n;
    /* 当前文件为light_monitor.csv，历史从.1到.4，.1最近。 */
    if (index)
        n = snprintf(path, PATH_MAX, "%s/light_monitor.csv.%u", logger->directory, index);
    else
        n = snprintf(path, PATH_MAX, "%s/light_monitor.csv", logger->directory);
    return n < 0 || n >= PATH_MAX ? ENAMETOOLONG : 0;
}

static int write_all(int fd, const char *data, size_t length)
{
    ssize_t count;
    while (length) {
        count = write(fd, data, length); /* 返回实际字节数，可能短写或被信号打断。 */
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return errno;
        }
        if (!count)
            return EIO;
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

/* 失败行尽量回退；回退或同步失败则禁止对同一inode继续追加。 */
static void rollback(struct lm_logger *logger, off_t before)
{
    struct stat status;
    if (ftruncate(logger->fd, before) == 0 && fdatasync(logger->fd) == 0)
        return;
    logger->tainted = 1;
    if (fstat(logger->fd, &status) == 0) {
        logger->tainted_dev = status.st_dev;
        logger->tainted_ino = status.st_ino;
    }
    pthread_mutex_lock(&logger->lock);
    ++logger->stats.uncertain;
    pthread_mutex_unlock(&logger->lock);
}

static int append_bytes(struct lm_logger *logger, const char *data, size_t length)
{
    off_t before = lseek(logger->fd, 0, SEEK_END);
    int error;
    if (before < 0)
        return errno;
    error = write_all(logger->fd, data, length);
    /* 只有整行写完且fdatasync成功，才增加saved。不是掉电零丢失保证。 */
    if (!error && fdatasync(logger->fd) < 0)
        error = errno;
    if (error)
        rollback(logger, before);
    return error;
}

static int open_log(struct lm_logger *logger)
{
    struct stat status;
    struct flock lock;
    char path[PATH_MAX], existing[sizeof(header)];
    int error;
    char last;

    /* mkdir仅创建这一级目录；父目录错误将被报告并按5秒重试。 */
    if (mkdir(logger->directory, 0755) < 0 && errno != EEXIST)
        return errno;
    if (logger->lock_fd < 0) {
        if (snprintf(path, sizeof(path), "%s/.light_monitor.lock", logger->directory)
            >= (int)sizeof(path))
            return ENAMETOOLONG;
        logger->lock_fd = open(path, O_RDWR | O_CREAT | O_NONBLOCK, 0644);
        if (logger->lock_fd < 0)
            return errno;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        /* 非阻塞建议锁避免本应用的两个进程同时轮换同一目录。 */
        if (fcntl(logger->lock_fd, F_SETLK, &lock) < 0) {
            error = errno;
            close(logger->lock_fd);
            logger->lock_fd = -1;
            return error;
        }
    }
    error = log_path(logger, 0, path);
    if (error)
        return error;
    logger->fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_NONBLOCK, 0644);
    if (logger->fd < 0)
        return errno;
    if (fstat(logger->fd, &status) < 0)
        return errno;
    if (!S_ISREG(status.st_mode))
        return EINVAL;
    if (logger->tainted && (logger->tainted_ino == 0 ||
        (status.st_dev == logger->tainted_dev && status.st_ino == logger->tainted_ino)))
        return EIO;
    if (!status.st_size)
        return append_bytes(logger, header, sizeof(header) - 1);
    /* 追加前核对表头及完整尾行，避免把别的文件或损坏CSV静默当日志。 */
    if (status.st_size < (off_t)(sizeof(header) - 1) ||
        pread(logger->fd, existing, sizeof(header) - 1, 0) != (ssize_t)sizeof(header) - 1 ||
        memcmp(existing, header, sizeof(header) - 1))
        return EINVAL;
    if (pread(logger->fd, &last, 1, status.st_size - 1) != 1 || last != '\n')
        return EINVAL;
    return 0;
}

static int save_record(struct lm_logger *logger, const struct lm_record *record)
{
    char row[128], source[PATH_MAX], target[PATH_MAX];
    struct stat status;
    int length, index, error;

    length = snprintf(row, sizeof(row), "%" PRIu64 ",%" PRIu64 ",%u,%s\n",
                      record->sequence, record->elapsed_ms, record->raw,
                      record->alarm < 0 ? "DISABLED" : (record->alarm ? "ON" : "OFF"));
    if (length < 0 || length >= (int)sizeof(row))
        return EOVERFLOW;
    if (fstat(logger->fd, &status) < 0)
        return errno;
    if ((uint64_t)status.st_size + (unsigned int)length > logger->limit) {
        /* 先关闭已同步的当前文件，再从老到新移动，最多保留五个CSV。 */
        error = close(logger->fd) < 0 ? errno : 0;
        logger->fd = -1;
        if (error)
            return error;
        for (index = 3; index >= 0; --index) {
            error = log_path(logger, (unsigned int)index, source);
            if (!error)
                error = log_path(logger, (unsigned int)index + 1, target);
            if (error)
                return error;
            if (rename(source, target) < 0 && errno != ENOENT)
                return errno;
        }
        error = open_log(logger);
        if (error)
            return error;
    }
    return append_bytes(logger, row, (size_t)length);
}

/* 调用时持有lock；只修改统计，绝不进行文件I/O。 */
static void failed_locked(struct lm_logger *logger, int error)
{
    ++logger->stats.failures;
    logger->stats.last_errno = error;
    logger->stats.failed = 1;
    logger->stats.dropped += logger->count;
    logger->head = logger->count = 0;
}

static void *log_loop(void *argument)
{
    struct lm_logger *logger = argument;
    struct lm_record record;
    struct timespec retry;
    int error = 0, need_open = 1, wait_error;

    pthread_mutex_lock(&logger->lock);
    for (;;) {
        if (need_open) {
            if (logger->stats.failed) {
                if (logger->stopping)
                    break;
                /* 绝对单调时钟重试，虚假唤醒和新样本不能缩短5秒间隔。 */
                wait_error = pthread_cond_timedwait(&logger->changed, &logger->lock, &retry);
                if (!wait_error)
                    continue;
                if (wait_error != ETIMEDOUT) {
                    failed_locked(logger, wait_error);
                    break;
                }
                if (logger->stopping)
                    break;
            }
            pthread_mutex_unlock(&logger->lock);
            error = open_log(logger);
            if (error && logger->fd >= 0) {
                close(logger->fd);
                logger->fd = -1;
            }
            pthread_mutex_lock(&logger->lock);
            if (error) {
                failed_locked(logger, error);
                goto set_retry;
            }
            logger->stats.failed = 0;
            logger->stats.last_errno = 0;
            need_open = 0;
        }
        while (!logger->count && !logger->stopping)
            pthread_cond_wait(&logger->changed, &logger->lock);
        if (!logger->count && logger->stopping)
            break;
        record = logger->queue[logger->head];
        logger->head = (logger->head + 1) % LM_LOG_CAPACITY;
        --logger->count;
        logger->stats.in_flight = 1;
        pthread_mutex_unlock(&logger->lock);
        error = save_record(logger, &record);
        if (error && logger->fd >= 0) {
            close(logger->fd);
            logger->fd = -1;
        }
        pthread_mutex_lock(&logger->lock);
        logger->stats.in_flight = 0;
        if (!error) {
            ++logger->stats.saved;
            continue;
        }
        ++logger->stats.dropped; /* 已从队列取出的失败记录也必须计入。 */
        failed_locked(logger, error);
        need_open = 1;
set_retry:
        if (clock_gettime(CLOCK_MONOTONIC, &retry) < 0) {
            failed_locked(logger, errno);
            break;
        }
        retry.tv_sec += 5;
    }
    /* 停止或日志内部异常时，所有未写队列都明确归入丢弃。 */
    logger->stats.dropped += logger->count;
    logger->count = 0;
    logger->stopping = 1;
    pthread_mutex_unlock(&logger->lock);
    error = 0;
    if (logger->fd >= 0 && close(logger->fd) < 0)
        error = errno;
    if (logger->lock_fd >= 0 && close(logger->lock_fd) < 0 && !error)
        error = errno;
    if (error) {
        pthread_mutex_lock(&logger->lock);
        failed_locked(logger, error);
        pthread_mutex_unlock(&logger->lock);
    }
    return NULL;
}

int lm_logger_start(struct lm_logger *logger, const char *directory, unsigned int limit)
{
    pthread_condattr_t attribute;
    int error;
    memset(logger, 0, sizeof(*logger));
    if (strlen(directory) >= sizeof(logger->directory) - 32 || limit < 128)
        return EINVAL;
    strcpy(logger->directory, directory); /* 上面已检查目标容量。 */
    logger->limit = limit;
    logger->fd = logger->lock_fd = -1;
    error = pthread_mutex_init(&logger->lock, NULL);
    if (error)
        return error;
    error = pthread_condattr_init(&attribute);
    if (error)
        goto destroy_lock;
    error = pthread_condattr_setclock(&attribute, CLOCK_MONOTONIC);
    if (!error)
        error = pthread_cond_init(&logger->changed, &attribute);
    pthread_condattr_destroy(&attribute);
    if (error)
        goto destroy_lock;
    error = pthread_create(&logger->thread, NULL, log_loop, logger);
    if (!error)
        return 0;
    pthread_cond_destroy(&logger->changed);
destroy_lock:
    pthread_mutex_destroy(&logger->lock);
    return error;
}

void lm_logger_submit(struct lm_logger *logger, const struct lm_record *record)
{
    pthread_mutex_lock(&logger->lock);
    if (logger->stopping || logger->stats.failed || logger->count == LM_LOG_CAPACITY) {
        ++logger->stats.dropped;
    } else {
        logger->queue[(logger->head + logger->count) % LM_LOG_CAPACITY] = *record;
        ++logger->count;
        pthread_cond_signal(&logger->changed);
    }
    pthread_mutex_unlock(&logger->lock);
}

void lm_logger_snapshot(struct lm_logger *logger, struct lm_log_stats *out)
{
    pthread_mutex_lock(&logger->lock);
    *out = logger->stats;
    out->queued = logger->count;
    pthread_mutex_unlock(&logger->lock);
}

int lm_logger_stop(struct lm_logger *logger)
{
    int error;
    pthread_mutex_lock(&logger->lock);
    logger->stopping = 1;
    pthread_cond_broadcast(&logger->changed);
    pthread_mutex_unlock(&logger->lock);
    error = pthread_join(logger->thread, NULL);
    if (!error) {
        pthread_cond_destroy(&logger->changed);
        pthread_mutex_destroy(&logger->lock);
    }
    return error;
}
