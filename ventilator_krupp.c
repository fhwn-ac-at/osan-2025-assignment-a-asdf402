#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>


//////////////////////////////////////////////////////////////////////////////////////
// struct:
//////////////////////////////////////////////////////////////////////////////////////

// structure to store worker result
typedef struct
{
    int worker_id;
    pid_t pid;
    int tasks_done;
    int total_time;
} result_msg;


//////////////////////////////////////////////////////////////////////////////////////
// global:
//////////////////////////////////////////////////////////////////////////////////////

static char task_q_name[64];
static char result_q_name[64];


//////////////////////////////////////////////////////////////////////////////////////
// functions:
//////////////////////////////////////////////////////////////////////////////////////

/*
prints current time in HH:MM:SS format
take:   buffer pointer,
        length
return: nothing
*/
void timestamp(char *buf, size_t len)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    snprintf(buf, len, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/*
safely sends message, retries on EINTR
take:   message queue descriptor,
        message pointer, message length
return: 0 on success,
        -1 on failure
*/
int safe_mq_send(mqd_t mq, const char *msg, size_t len)
{
    while (1)
    {
        if (mq_send(mq, msg, len, 0) == 0)
        {
            return 0;
        }
        if (errno == EINTR)
        {
            continue;
        }

        return -1;
    }
}

/*
safely sends message, retries on EINTR
take:   message queue descriptor,
        message pointer, message length
return: 0 on success,
        -1 on failure
*/
ssize_t safe_mq_receive(mqd_t mq, char *buf, size_t len)
{
    ssize_t r;

    while (1)
    {
        r = mq_receive(mq, buf, len, NULL);

        if (r >= 0)
        {
            return r;
        }
        if (errno == EINTR)
        {
            continue;
        }

        return -1;
    }
}

/*
function executed by each worker process
take: integer worker_id
return:nothing
*/
void worker_main(int worker_id)
{
    mqd_t mq_tasks = mq_open(task_q_name, O_RDONLY);
    if (mq_tasks == (mqd_t)-1)
    {
        perror("Worker: mq_open task queue");
        exit(EXIT_FAILURE);
    }

    mqd_t mq_res = mq_open(result_q_name, O_WRONLY);
    if (mq_res == (mqd_t)-1)
    {
        perror("Worker: mq_open result queue");
        mq_close(mq_tasks);
        exit(EXIT_FAILURE);
    }

    char ts[16];

    timestamp(ts, sizeof(ts));
    printf("%s | Worker #%02d | Started worker PID %d\n", ts, worker_id, getpid());
    fflush(stdout);

    int count = 0, total = 0;

    while (1)
    {
        int effort;

        if (safe_mq_receive(mq_tasks, (char*)&effort, sizeof(effort)) < 0)
        {
            perror("Worker: mq_receive");
            break;
        }

        timestamp(ts, sizeof(ts));

        if (effort == 0)
        {
            printf("%s | Worker #%02d | Received termination task\n", ts, worker_id);
            fflush(stdout);

            result_msg res =
            {
                .worker_id = worker_id,
                .pid = getpid(),
                .tasks_done = count,
                .total_time = total
            };

            if (safe_mq_send(mq_res, (char*)&res, sizeof(res)) < 0)
            {
                perror("Worker: mq_send result");
            }

            break;
        }

        printf("%s | Worker #%02d | Received task with effort %d\n", ts, worker_id, effort);
        fflush(stdout);
        sleep(effort);

        count++;
        total += effort;
    }

    mq_close(mq_tasks);
    mq_close(mq_res);
    exit(EXIT_SUCCESS);
}

/*
short description: main ventilator function, sets up everything
take: argc and argv (main arguments)
return: exit status
*/
int ventilator_main(int argc, char *argv[])
{
    int opt, workers = -1, tasks = -1, qsize = -1;

    while ((opt = getopt(argc, argv, "w:t:s:")) != -1)
    {
        switch (opt)
        {
            case 'w': workers = atoi(optarg); break;
            case 't': tasks  = atoi(optarg); break;
            case 's': qsize  = atoi(optarg); break;
            default:
            {
                fprintf(stderr, "Usage: %s -w <workers> -t <tasks> -s <queue_size>\n", argv[0]);
                return EXIT_FAILURE;
            }
        }
    }

    if (workers <= 0 || tasks < 0 || qsize <= 0)
    {
        fprintf(stderr, "Invalid arguments; all must be positive integers.\n");
        return EXIT_FAILURE;
    }

    // unique queue names with parent PID
    snprintf(task_q_name,   sizeof(task_q_name),   "/task_q_%d",   getpid());
    snprintf(result_q_name, sizeof(result_q_name), "/res_q_%d",    getpid());

    // cleanup any old remnants
    mq_unlink(task_q_name);
    mq_unlink(result_q_name);

    // create task queue
    struct mq_attr attr_task =
    {
        .mq_flags = 0,
        .mq_maxmsg = qsize,
        .mq_msgsize = sizeof(int),
        .mq_curmsgs = 0
    };

    mqd_t mq_tasks = mq_open(task_q_name, O_CREAT | O_WRONLY, 0644, &attr_task);
    if (mq_tasks == (mqd_t)-1)
    {
        perror("Ventilator: mq_open task queue");
        return EXIT_FAILURE;
    }

    // create result queue
    struct mq_attr attr_res =
    {
        .mq_flags = 0,
        .mq_maxmsg = workers,
        .mq_msgsize = sizeof(result_msg),
        .mq_curmsgs = 0
    };

    mqd_t mq_res = mq_open(result_q_name, O_CREAT | O_RDONLY, 0644, &attr_res);
    if (mq_res == (mqd_t)-1)
    {
        perror("Ventilator: mq_open result queue");
        mq_unlink(task_q_name);
        mq_close(mq_tasks);
        return EXIT_FAILURE;
    }

    char ts[16];

    timestamp(ts, sizeof(ts));

    printf("%s | Ventilator | Starting %d workers for %d tasks and a queue size of %d\n",
           ts, workers, tasks, qsize);

    fflush(stdout);

    pid_t *pids = calloc(workers, sizeof(pid_t));
    if (!pids)
    {
        perror("Ventilator: calloc");
        goto cleanup_queues;
    }

    // fork workers
    for (int i = 1; i <= workers; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("Ventilator: fork");
            goto cleanup_children;
        }
        if (pid == 0)
        {
            // child = worker
            mq_close(mq_tasks);
            mq_close(mq_res);
            worker_main(i);
        }

        pids[i-1] = pid;

        timestamp(ts, sizeof(ts));

        printf("%s | Worker #%02d | Started worker PID %d\n", ts, i, pid);
        fflush(stdout);
    }

    // distribute tasks
    timestamp(ts, sizeof(ts));
    printf("%s | Ventilator | Distributing tasks\n", ts);

    fflush(stdout);
    srand(time(NULL));

    for (int i = 1; i <= tasks; i++)
    {
        int effort = (rand() % 10) + 1;

        timestamp(ts, sizeof(ts));
        printf("%s | Ventilator | Queuing task #%d with effort %d\n", ts, i, effort);
        fflush(stdout);

        if (safe_mq_send(mq_tasks, (char*)&effort, sizeof(effort)) < 0)
        {
            perror("Ventilator: mq_send task");
        }
    }

    // send termination tasks
    timestamp(ts, sizeof(ts));
    printf("%s | Ventilator | Sending termination tasks\n", ts);
    fflush(stdout);

    int term = 0;

    for (int i = 0; i < workers; i++)
    {
        if (safe_mq_send(mq_tasks, (char*)&term, sizeof(term)) < 0)
        {
            perror("Ventilator: mq_send term");
        }
    }

    mq_close(mq_tasks);

    // collect results
    timestamp(ts, sizeof(ts));
    printf("%s | Ventilator | Waiting for workers to terminate\n", ts);
    fflush(stdout);

    for (int i = 0; i < workers; i++)
    {
        result_msg res;

        if (safe_mq_receive(mq_res, (char*)&res, sizeof(res)) < 0)
        {
            perror("Ventilator: mq_receive result");
            continue;
        }

        timestamp(ts, sizeof(ts));
        printf("%s | Ventilator | Worker %d processed %d tasks in %d seconds\n",
               ts, res.worker_id, res.tasks_done, res.total_time);
        fflush(stdout);
    }

    // reap children
    for (int i = 0; i < workers; i++)
    {
        int status;
        pid_t w = waitpid(pids[i], &status, 0);

        timestamp(ts, sizeof(ts));
        printf("%s | Ventilator | Worker %d with PID %d exited with status %d\n",
               ts, i+1, w, WEXITSTATUS(status));
        fflush(stdout);
    }

cleanup_children:
    free(pids);

cleanup_queues:
    mq_close(mq_res);
    mq_unlink(task_q_name);
    mq_unlink(result_q_name);
    return EXIT_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////////////
// main function:
//////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    return ventilator_main(argc, argv);
}
