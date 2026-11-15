/* 解析命令并调用统一业务接口，不直接操作终端、GPIO或I2C。 */
#define _POSIX_C_SOURCE 200809L
#include "light_monitor.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *lm_alarm_name(const struct lm_snapshot *state)
{
    return !state->threshold_enabled ? "DISABLED" : (state->alarm ? "ON" : "OFF");
}

static const char *buzzer_name(const struct lm_snapshot *state)
{
    return !state->buzzer_known ? "UNKNOWN" : (state->buzzer_on ? "ON" : "OFF");
}

static int number(const char *text, unsigned int maximum, unsigned int *out)
{
    const char *cursor;
    char *end;
    unsigned long value;
    if (!*text)
        return -1;
    for (cursor = text; *cursor; ++cursor)
        if (*cursor < '0' || *cursor > '9')
            return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value > maximum)
        return -1;
    *out = (unsigned int)value;
    return 0;
}

void lm_command(const char *line, struct lm_sampler *sampler, char *reply, size_t size)
{
    struct lm_snapshot state;
    char copy[128], *save, *token, *arguments[4], raw[24];
    size_t count = 0;
    unsigned int first = 0, second = 0;
    enum lm_action action;
    int error;

    if (strlen(line) >= sizeof(copy)) {
        snprintf(reply, size, "ERR invalid_arguments");
        return;
    }
    strcpy(copy, line); /* 上面已验证容量，不会截断后误执行命令。 */
    token = strtok_r(copy, " \t\r\v\f", &save);
    while (token && count < 4) {
        arguments[count++] = token;
        token = strtok_r(NULL, " \t\r\v\f", &save);
    }
    if (!count) {
        snprintf(reply, size, "ERR unknown_command");
        return;
    }
    if (!strcmp(arguments[0], "STATUS")) {
        if (count != 1)
            goto invalid;
        lm_sampler_snapshot(sampler, &state);
        if (state.valid)
            snprintf(raw, sizeof(raw), "%u", state.raw);
        else
            snprintf(raw, sizeof(raw), "NA");
        snprintf(reply, size,
                 "STATUS state=%s valid=%d ALS_RAW=%s attempts=%" PRIu64
                 " successes=%" PRIu64 " errors=%" PRIu64 " missed=%" PRIu64
                 " elapsed_ms=%" PRIu64 " interval_ms=%u last_errno=%d alarm=%s"
                 " threshold_enabled=%d low=%u high=%u buzzer=%s control_errno=%d"
                 " control_errors=%" PRIu64 " key_enabled=%d key_errno=%d key_errors=%" PRIu64
                 " superseded=%" PRIu64 " log_saved=%" PRIu64 " log_dropped=%" PRIu64
                 " log_errors=%" PRIu64 " log_queue=%u log_inflight=%u"
                 " log_failed=%d log_errno=%d log_uncertain=%" PRIu64,
                 state.running ? "RUNNING" : "PAUSED", state.valid, raw,
                 state.attempts, state.successes, state.errors, state.missed,
                 state.elapsed_ms, state.interval_ms, state.last_errno,
                 lm_alarm_name(&state), state.threshold_enabled, state.low, state.high,
                 buzzer_name(&state), state.control_errno, state.control_errors,
                 state.key_enabled, state.key_errno, state.key_errors, state.superseded,
                 state.log.saved, state.log.dropped, state.log.failures,
                 state.log.queued, state.log.in_flight, state.log.failed,
                 state.log.last_errno, state.log.uncertain);
        return;
    }
    if (!strcmp(arguments[0], "START") || !strcmp(arguments[0], "STOP")) {
        if (count != 1)
            goto invalid;
        action = !strcmp(arguments[0], "START") ? LM_START : LM_STOP;
    } else if (!strcmp(arguments[0], "SET_INTERVAL")) {
        if (count != 2 || number(arguments[1], 60000, &first) || first < 100)
            goto invalid;
        action = LM_INTERVAL;
    } else if (!strcmp(arguments[0], "SET_THRESHOLD")) {
        if (count != 3 || number(arguments[1], 65535, &first) ||
            number(arguments[2], 65535, &second) || first >= second)
            goto invalid;
        action = LM_THRESHOLD;
    } else {
        snprintf(reply, size, "ERR unknown_command");
        return;
    }
    error = lm_sampler_control(sampler, action, first, second);
    lm_sampler_snapshot(sampler, &state);
    /* 合法配置可能已应用但蜂鸣器关闭失败，必须显式告知applied=1。 */
    snprintf(reply, size,
             "%s command=%s applied=%d state=%s interval_ms=%u low=%u high=%u"
             " alarm=%s buzzer=%s control_errno=%d",
             error ? "ERR control_failed" : "OK", arguments[0], error != ECANCELED,
             state.running ? "RUNNING" : "PAUSED", state.interval_ms,
             state.low, state.high, lm_alarm_name(&state), buzzer_name(&state), error);
    return;
invalid:
    snprintf(reply, size, "ERR invalid_arguments");
}
