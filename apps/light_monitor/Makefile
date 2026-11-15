# 用户程序独立构建，不修改上层内核模块Makefile。
CC = $(CROSS_COMPILE)gcc
CFLAGS = -std=gnu99 -Wall -Wextra -Werror -O2 -pthread
LDLIBS = -pthread -lrt
SOURCES = main.c sampler.c sensor.c command.c logger.c buzzer.c

all: light_monitor

light_monitor: $(SOURCES) light_monitor.h logger.h buzzer.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) $(LDLIBS) -o $@

native: $(SOURCES) light_monitor.h logger.h buzzer.h
	gcc $(CFLAGS) $(SOURCES) $(LDLIBS) -o light_monitor_native

.PHONY: all native
