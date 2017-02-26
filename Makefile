# Build an executable named My_Alarm from My_Alarm.c

all: My_Alarm.c
	cc My_Alarm.c -o My_Alarm -D_POSIX_PTHREAD_SEMANTICS -lpthread
