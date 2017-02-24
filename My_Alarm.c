/*
 * alarm_mutex.c
 *
 * This is an enhancement to the alarm_thread.c program, which
 * created an "alarm thread" for each alarm command. This new
 * version uses a single alarm thread, which reads the next
 * entry in a list. The main thread places new requests onto the
 * list, in order of absolute expiration time. The list is
 * protected by a mutex, and the alarm thread sleeps for at
 * least 1 second, each iteration, to ensure that the main
 * thread can lock the mutex to add new work to the list.
 */
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include "errors.h"

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    int                 display_thread_id; /* Either 1 or 2   */
    char                message[64];
} alarm_t;

pthread_mutex_t alarm_mutex_odd = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list_odd = NULL;

pthread_mutex_t alarm_mutex_even = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list_even = NULL;

int alarmPipe[2];
int odd_id = 1;
int even_id = 2;

// Make a new function as there are issues with process_alarm function
void process_alarms(alarm_t *alarm_list) {
    
}

void process_alarm(alarm_t *alarm) {
   int secondsLeft;
    time_t now;
    // A3.4 (a)
    printf("Display Thread <%d>: Received Alarm Request at <%ld>: <%d %s>, ExpiryTime is <%ld>\n", 
        alarm->display_thread_id, time(NULL), alarm->seconds, alarm->message, alarm->time);
    
    now = time(NULL);
    secondsLeft = alarm->seconds;
    
    // loop and print 
    do {
        // A3.4 (b) 
        printf("Display Thread <%d>: Number of SecondsLeft <%d>: Time: <%ld>: <%d %s>\n",
            alarm->display_thread_id, secondsLeft, time(NULL), alarm->seconds, alarm->message);
            
        if(secondsLeft < 2) {
            sleep(secondsLeft);
            now++;
            secondsLeft--;
        } else {
            sleep(2);
            now += 2;
            secondsLeft -= 2;
        }
    } while(now < alarm->time);
    //A3.4 (c)
    printf("Display Thread <%d>: Alarm Expired at <%ld>: <%d %s>\n",
        alarm->display_thread_id, time(NULL), alarm->seconds, alarm->message);
}

void *display_thread (void *arg) {
    int *thread_id = (int*)arg;
    int status;
    int sleepTime;
    alarm_t *alarm, **alarm_list;
    pthread_mutex_t *list_mutex;

    if (*thread_id == 1) {
        alarm_list = &alarm_list_odd;
        list_mutex = &alarm_mutex_odd;
    } else if (*thread_id == 2) {
        alarm_list = &alarm_list_even;
        list_mutex = &alarm_mutex_even;
    } else {
        return 0;
    }

    while (1) {
        status = pthread_mutex_lock (list_mutex);
        if (status != 0) err_abort (status, "Lock mutex");

        alarm = *alarm_list;
        if (alarm == NULL) {
            sleepTime = 0.5;
	    //sleepTime = 1;
        } else {
            process_alarm(alarm);
            // do magic, find new sleep time and print if needed
            alarm_list = &alarm->link;
        }

        status = pthread_mutex_unlock (list_mutex);
        if (status != 0) err_abort (status, "Unlock mutex");

        if (sleepTime > 0)
            sleep (sleepTime);
        else
            sched_yield ();
    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg) {
    alarm_t *alarm;
    int status;
    alarm_t **alarm_list, **last, *next;
    pthread_mutex_t *list_mutex;

    while (1) {
        status = read(alarmPipe[0], &alarm, sizeof(alarm));
        if (status <= 0) err_abort (status, "Pipe read");

        if (alarm->time % 2 == 0) {
            alarm->display_thread_id = even_id;
            alarm_list = &alarm_list_even;
            list_mutex = &alarm_mutex_even;
        } else {
            alarm->display_thread_id = odd_id;
            alarm_list = &alarm_list_odd;
            list_mutex = &alarm_mutex_odd;
        }

        status = pthread_mutex_lock (list_mutex);
        if (status != 0) err_abort (status, "Lock mutex");
        // insert into the list in a sorted order
        last = alarm_list;
        next = *last;
        while (next != NULL) {
            if (next->time >= alarm->time) {
                alarm->link = next;
                *last = alarm;
                break;
            }
            last = &next->link;
            next = next->link;
        }
        if (next == NULL) {
            *last = alarm;
            alarm->link = NULL;
        }
        status = pthread_mutex_unlock (list_mutex);
        if (status != 0) err_abort (status, "Unlock mutex");

        // print statement
        printf("Alarm Thread Passed on Alarm Request to Display Thread <%d> at <%ld>: <%d %s>\n", 
            alarm->display_thread_id, time(NULL), alarm->seconds, alarm->message);
    }
}

int main (int argc, char *argv[])
{
    int status;
    char line[128];
    alarm_t *alarm;
    pthread_t thread, display_thread_1, display_thread_2;

    status = pipe(alarmPipe);
    if (status != 0) err_abort (status, "Create pipe");

    status = pthread_create (&thread, NULL, alarm_thread, NULL);
    if (status != 0) err_abort (status, "Create alarm thread");

    // Create two additional threads: display_thread_1
    status = pthread_create (&display_thread_1, NULL, display_thread, &odd_id);
    if (status != 0) err_abort (status, "Create display thread 1");
    // and display_thread_2
    status = pthread_create (&display_thread_2, NULL, display_thread, &even_id);
    if (status != 0) err_abort (status, "Create display thread 2");

    while (1) {
        printf ("alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL) 
            errno_abort ("Allocate alarm");

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
        if (sscanf (line, "%d %64[^\n]", &alarm->seconds, alarm->message) < 2 || alarm->seconds < 0) {
            fprintf (stderr, "Bad command\n");
            free (alarm);
        } else {
            alarm->time = time (NULL) + alarm->seconds; // Expiry time

            // A3.2
            printf("Main Thread Received Alarm Request at <%ld>: <%d %s>\n", 
                alarm->time - alarm->seconds, alarm->seconds, alarm->message);

            status = write(alarmPipe[1], &alarm, sizeof(alarm));
            if (status < 0) err_abort (status, "Pipe write");
        }
    }
}
