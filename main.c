#include "command.h"
#include "job.h"
#include "parse.h"
#include "run.h"
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

void print_job_list(job *);

const struct command commands[] = {
    {.name = "fg\n", .func = command_fg},
    {.name = "bg\n", .func = command_bg},
    {NULL, NULL}, // indicates tail
};

int main(int argc, char *argv[]) {
    char s[LINELEN];
    job *curr_job;
    bool is_default_command;

    init_shell();

    while (get_line(s, LINELEN)) {
        is_default_command = false;

        if (!strcmp(s, "exit\n"))
            break;
        signal(SIGCHLD, SIG_BLOCK);
        for (const struct command *c = commands; c->name; c++) {
            if (!strcmp(s, c->name)) {
                is_default_command = true;
                c->func();
            }
        }
        signal(SIGCHLD, sigchld_handler);
        if (is_default_command)
            continue;
        curr_job = parse_line(s);

        if (curr_job)
            add_job(&running_job, curr_job);
        signal(SIGCHLD, SIG_BLOCK);
        run_job(curr_job);
        signal(SIGCHLD, sigchld_handler);
    }

    return 0;
}
