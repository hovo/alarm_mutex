/*
 * My_Alarm.c
 *
 * This is an enhancement to the alarm_mutex.c program, which
 * used a single alarm thread. This new version will have a
 * main thread which creates an alarm thread and two display
 * threads. Once the alarm thread has retrieved each alarm
 * request from the alarm list, it will pass the alarm request
 * to one of two display threads:
 *
 * - display_thread_1 which will handle the alarm request if
 *   its expiry time is closer to an odd integer
 *
 * - display_thread_2 which will handle the alarm request if
 *   its expiry time is closer to an even integer
 *
 * and then print an output every two seconds until the alarm
 * request expires.
 */
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include "errors.h"

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, and a display_thread_id
 * which will either be 1 or 2.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time; /* Seconds from EPOCH */
    int                 display_thread_id; /* Either 1 or 2 */
    char                message[64];
} alarm_t;

pthread_mutex_t alarm_mutex_odd = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list_odd = NULL;

pthread_mutex_t alarm_mutex_even = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list_even = NULL;

// Declare variables for pipe and odd/even display thread IDs.
int alarmPipe[2];
int odd_id = 1;
int even_id = 2;

// Make a new function as there are issues with process_alarm function.
void process_alarms(alarm_t *alarm_list) {

}

void process_alarm(alarm_t *alarm) {
   int secondsLeft;
    time_t now;

    /*
     * A3.4 (a)
     * Showcases which display thread received the alarm request and
     * accordingly outputs the time at which the alarm request was
     * received, the Alarm Request itself, and its ExpiryTime.
     */
    printf("Display Thread <%d>: Received Alarm Request at <%ld>: <%d %s>, ExpiryTime is <%ld>\n",
        alarm->display_thread_id, time(NULL), alarm->seconds, alarm->message, alarm->time);

    now = time(NULL);
    secondsLeft = alarm->seconds;

    /*
     * A3.4 (b)
     * While the current time is lesser than the expected expiry time,
     * the program will loop and print the number of seconds left with
     * the current time every two seconds. It accomplishes this by
     * modifying its relevant values by two anytime there are more
     * than two seconds left and two seconds pass. Otherwise, it
     * just runs the clock out until expiry time.
     */
    do {
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

    /*
     * A3.4 (c)
     * This is when the alarm request has expired. As such, this
     * portion of the code will allow the display thread to output
     * the fact that it has expired and its expiration time and
     * alarm request.
     */
    printf("Display Thread <%d>: Alarm Expired at <%ld>: <%d %s>\n",
        alarm->display_thread_id, time(NULL), alarm->seconds, alarm->message);
}

/*
 * Function responsible for processing display_thread_1 and display_thread_2.
 */
void *display_thread (void *arg) {
    int *thread_id = (int*)arg;
    int status;
    int sleepTime;
    alarm_t *alarm, **alarm_list;
    pthread_mutex_t *list_mutex;

    // Get pointers to the odd/even lists and mutexes depending on the thread ID.
    if (*thread_id == 1) {
        alarm_list = &alarm_list_odd;
        list_mutex = &alarm_mutex_odd;
    } else if (*thread_id == 2) {
        alarm_list = &alarm_list_even;
        list_mutex = &alarm_mutex_even;
    } else {
        return 0;
    }

    // Process all queued alarms.
    while (1) {
        status = pthread_mutex_lock (list_mutex);
        if (status != 0) err_abort (status, "Lock mutex");

        // If alarm list is empty, wait for 0.5 seconds to allow main thread to read another command.
        alarm = *alarm_list;
        if (alarm == NULL) {
            sleepTime = 0.5;
        } else {
            process_alarm(alarm);
            alarm_list = &alarm->link;
        }

        // Unlock mutex before waiting to allow main thread to insert more alarms.
        status = pthread_mutex_unlock (list_mutex);
        if (status != 0) err_abort (status, "Unlock mutex");

        // sched_yield causes the calling thread to relinquish the CPU.
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

    // Loop forever, processing commands.
    while (1) {
        status = read(alarmPipe[0], &alarm, sizeof(alarm));
        if (status <= 0) err_abort (status, "Pipe read");

        /*
         * If the alarm expiry time is closer to an odd integer, pass the
         * alarm to display_thread_1, and if it is closer to an even integer,
         * pass the alarm to display_thread_2. This is achieved by the
         * modulus operator, passing the alarm request to an already created
         * POSIX Thread with display_thread_id = 2 if the expiry time is closer
         * to an even integer. Otherwise, the alarm request will be passed
         * to a different POSIX Thread that with display_thread_id = 1. Once
         * that is done, the program will output which display thread the
         * alarm request has been passed to. Mutexes and odd/even alarm
         * lists will be accordingly applied.
         */
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

        // Insert into the list in a sorted order.
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

        // Print statement.
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

    // Implementation of a pipe.
    status = pipe(alarmPipe);
    if (status != 0) err_abort (status, "Create pipe");

    status = pthread_create (&thread, NULL, alarm_thread, NULL);
    if (status != 0) err_abort (status, "Create alarm thread");

    // Create two additional threads: display_thread_1,
    status = pthread_create (&display_thread_1, NULL, display_thread, &odd_id);
    if (status != 0) err_abort (status, "Create display thread 1");
    // and display_thread_2.
    status = pthread_create (&display_thread_2, NULL, display_thread, &even_id);
    if (status != 0) err_abort (status, "Create display thread 2");

    // Alarm prompt.
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
         * separated from the seconds by whitespace. This
         * section has been slightly modified from the original
         * alarm_mutex.c file and will handle an extra condition:
         * whether the seconds for the input line is negative.
         */
        if (sscanf (line, "%d %64[^\n]", &alarm->seconds, alarm->message) < 2 || alarm->seconds < 0) {
            fprintf (stderr, "Bad command\n");
            free (alarm);
        } else {
            alarm->time = time (NULL) + alarm->seconds; // Expiry time.

            /*
             * A3.2
             * This segment of the code will output that the main thread
             * has received an alarm request along with the time it was
             * received and the alarm request itself.
             */
            printf("Main Thread Received Alarm Request at <%ld>: <%d %s>\n",
                alarm->time - alarm->seconds, alarm->seconds, alarm->message);

            status = write(alarmPipe[1], &alarm, sizeof(alarm));
            if (status < 0) err_abort (status, "Pipe write");
        }
    }
}
