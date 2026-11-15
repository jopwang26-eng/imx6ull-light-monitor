#ifndef LIGHT_MONITOR_BUZZER_H
#define LIGHT_MONITOR_BUZZER_H
/* 由业务锁保护的独占蜂鸣器实例；失败会关闭旧fd以允许重新绑定恢复。 */
struct lm_buzzer { const char *path; int fd; };
int lm_buzzer_set(struct lm_buzzer *buzzer, unsigned char value);
int lm_buzzer_close(struct lm_buzzer *buzzer);
#endif
