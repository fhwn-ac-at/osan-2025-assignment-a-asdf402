#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include <unistd.h> // für execlp
//#include <.h> //? neuer header
#include <sys/wait.h>
#include <time.h>



typedef struct command_line_arguments
{
    int i;
    char const *s;
    bool b;
} cli_args;

cli_args parse_command_line(int const argc, char *argv[])
{
    cli_args args = {0, NULL, false};

    int optgot = -1;
    do
    {
        optgot = getopt(argc, argv, "i:s:b");

        switch (optgot)
        {
            case 'i':
                args.i = atoi(optarg);
                break;
            case 's':
                args.s = optarg;
                break;
            case 'b':
                args.b = true;
                break;
            case '?':
                printf("usage: %s -i <number> -s <string> -b\n", argv[0]);   
                exit(EXIT_FAILURE);
            default:
                break;
        }
        
    } while (optgot != -1);

    if(args.i <= 0 || strlen(args.s) <5)
    {
        printf("\nusage: i: %d, s: %s, b: %d\n", args.i, args.s, args.b);
        exit(EXIT_FAILURE);
    }

    return args;
}

int child_labour()
{
//    printf("I am %d, child of %d\n", getpid(), getppid());

    printf("[%d] doing some work for (%d)\n", getpid(), getppid());
    srand(getpid());
    sleep(2);
    printf("[%d] jobs done\n", getpid());
    printf("[%d] bringing coal to (%d)\n", getpid(), getppid());

    // *(int *)(17) = 0; // führt zu absturz

    return -1;
}

int main(int argc, char *argv[])
{
    /*
    cli_args const args = parse_command_line(argc, argv);
    printf("\ni: %d, s: %s, b: %d\n", args.i, args.s, args.b);
    */


    printf("[%d] sending child into the mines...\n", getpid());

    pid_t forked = fork(); //pid_t für PID (int auch möglich, nur form sache)
    if (forked == 0)
    {
        // execlp("ls", "ls", "-l", NULL);
        return child_labour();
    }

    printf("my PID is %d, fork returned %d\n", getpid(), forked);

    printf("[%d] enjoying some brandy...\n", getpid());
    printf("[%d] where the fudge is my coal?\n", getpid());

    for (int i = 0; i < 10; ++i)
    {
        int wstatus = 0;
        pid_t const waited = wait(&wstatus);

        if(WIFEXITED(wstatus))
        {
            printf("[%d] child %d, exited normally with return code %d\n", getpid(), waited, WEXITSTATUS(wstatus));
        }
        else if (WIFSIGNALED(wstatus))
        {
            printf("[%d] child %d, terminated with signal %d\n", getpid(), waited, WTERMSIG(wstatus));
        }
        else
        {
            printf("[%d] child %d, terminated abnormaly %d\n", getpid(), waited, WTERMSIG(wstatus));
        }
    }

    printf("all children have returned\n");

    return 0;
}
