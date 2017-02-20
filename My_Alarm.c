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
    int                 thread_id;
    char                message[64];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list = NULL;

void *display_thread(void *arg) {
    alarm_t *alarm = (alarm_t*)arg;
    int secondsLeft;
    time_t now;
    int end_time;

    printf("Display <%d>: Received Alarm Request at <%ld>: <%d %s>, ExpiryTime is <%ld>\n", 
        alarm->thread_id, alarm->time, alarm->seconds, alarm->message, alarm->time + alarm->seconds);
    
    now = time(NULL);
    end_time = now + alarm->seconds;
    secondsLeft = alarm->seconds;
    // loop and print 
    do {
        printf("Display Thread <%d>: Number of SecondsLeft <%d>: Time: <%ld>: <%d %s>\n",
            alarm->thread_id, secondsLeft, time(NULL), alarm->seconds, alarm->message);
        if(secondsLeft < 2) {
            sleep(1);
            now++;
            secondsLeft--;
        } else {
            sleep(2);
            now += 2;
            secondsLeft -= 2;
        }
    } while(now < end_time);

    printf("Display Thread <%d>: Alarm Expired at <%ld>: <%d %s>\n",
        alarm->thread_id, time(NULL), alarm->seconds, alarm->message);

    free(alarm);
    pthread_exit(0);    
}

void printMessage(int status, alarm_t *alarm) {
    if(status == 0) {
        printf("Alarm Thread Passed on Alarm Request to Display Thread <%d> at <%ld>: <%d %s>\n", 
            alarm->thread_id, time(NULL), alarm->seconds, alarm->message);
    }         
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg) {
    alarm_t *alarm;
    int sleep_time;
    int status;
    time_t now;

    pthread_t display_thread_1, display_thread_2;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits.
     */
    while (1) {
        alarm = alarm_list;
        /*
         * If the alarm list is empty, wait for one second. This
         * allows the main thread to run, and read another
         * command. If the list is not empty, remove the first
         * item. Compute the number of seconds to wait -- if the
         * result is less than 0 (the time has passed), then set
         * the sleep_time to 0.
         */
        if (alarm == NULL) {
            sleep_time = 1;
        }    
        else {
            alarm_list = alarm->link;
            // A3.3
            if(alarm->time % 2 != 0 ) {
                alarm->thread_id = 1;
                status = pthread_create(&display_thread_1, NULL, display_thread, alarm);
                printMessage(status, alarm);
            } else {
                alarm->thread_id = 2;
                status = pthread_create(&display_thread_2, NULL, display_thread, alarm);
                printMessage(status, alarm);
            }
            // A3.3
            
        }
    }
}

int main (int argc, char *argv[]) {
    int status;
    char line[128];
    alarm_t *alarm, **last, *next;
    pthread_t thread;

    status = pthread_create (
        &thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort (status, "Create alarm thread");
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
        if (sscanf (line, "%d %64[^\n]", 
            &alarm->seconds, alarm->message) < 2) {
            fprintf (stderr, "Bad command\n");
            free (alarm);
        } else {
            status = pthread_mutex_lock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
           // alarm->time = time (NULL) + alarm->seconds;
           alarm->time = time (NULL);
            // A3.2
            printf("Main Thread Received Alarm Request at <%ld>: <%d %s>\n", alarm->time, alarm->seconds, alarm->message);

            /*
             * Insert the new alarm into the list of alarms,
             * sorted by expiration time.
             */
            last = &alarm_list;
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
            /*
             * If we reached the end of the list, insert the new
             * alarm there. ("next" is NULL, and "last" points
             * to the link field of the last item, or to the
             * list header).
             */
            if (next == NULL) {
                *last = alarm;
                alarm->link = NULL;
            }
#ifdef DEBUG
            printf ("[list: ");
            for (next = alarm_list; next != NULL; next = next->link)
                printf ("%d(%d)[\"%s\"] ", next->time,
                    next->time - time (NULL), next->message);
            printf ("]\n");
#endif
            status = pthread_mutex_unlock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
