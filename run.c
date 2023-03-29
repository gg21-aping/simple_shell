#include "run.h"
#include "job.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

void run_child(process *p, pid_t pgid, int infile, int outfile, job_mode mode) {
    pid_t pid;

    if (shell_is_interactive) {
        /* Put the process into the process group and give the process group
         the terminal, if appropriate.
         This has to be done both by the shell and in the individual
         child processes because of potential race conditions.  */
        pid = getpid();
        if (pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);
        if (mode == FOREGROUND)
            tcsetpgrp(shell_terminal, pgid);

        /* Set the handling for job control signals back to the default.  */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
    }

    /* Set the standard input/output channels of the new process.  */
    if (infile != STDIN_FILENO) {
        dup2(infile, STDIN_FILENO);
        close(infile);
    }
    if (outfile != STDOUT_FILENO) {
        dup2(outfile, STDOUT_FILENO);
        close(outfile);
    }

    execvp(p->program_name, p->argument_list);
    perror("execvp");
    exit(EXIT_FAILURE);
}

// return process group id
pid_t run_process(process *p, pid_t pgid, int infile, int outfile,
                  job_mode mode) {
    pid_t pid;

    if ((pid = fork()) == 0)
        run_child(p, pgid, infile, outfile, mode);
    return pid;
}

void run_job(job *j) {
    int infile, outfile, open_flag;
    int fd[2] = {-1, -1};
    pid_t pid;

    if (!j)
        return;

    infile = STDIN_FILENO;

    for (process *p = j->process_list; p != NULL; p = p->next) {
        if (p->next) {
            if (-1 == pipe(fd)) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
            outfile = fd[1];
        } else
            outfile = STDOUT_FILENO;
        // redirection
        if (p->input_redirection) {
            if (infile != STDIN_FILENO)
                close(infile);
            if (-1 == (infile = open(p->input_redirection, O_RDONLY)))
                goto error_input_redirection;
        }
        if (p->output_redirection) {
            if (outfile != STDOUT_FILENO)
                close(outfile);
            open_flag =
                O_WRONLY | (p->output_option == TRUNC ? O_TRUNC | O_CREAT : O_APPEND);
            if (-1 == (outfile = open(p->output_redirection, open_flag, 0664))) {
                goto error_output_redirection;
            }
        }

        pid = run_process(p, j->pgid, infile, outfile, j->mode);
        p->pid = pid;
        if (shell_is_interactive) {
            if (!j->pgid)
                j->pgid = pid;
            setpgid(pid, j->pgid);
        }
        /* Clean up after pipes.  */
        if (infile != STDIN_FILENO)
            close(infile);
        if (outfile != STDOUT_FILENO)
            close(outfile);
        infile = fd[0];
    }
    if (!shell_is_interactive)
        wait_for_job(j);
    else if (j->mode == FOREGROUND) {
        put_job_in_foreground(j, 0);
    } else
        put_job_in_background(j, 0);
    return;

error_output_redirection:
    if (infile != STDIN_FILENO)
        close(infile);
error_input_redirection:
    return;
}

void run_jobs(job *j) {
    for (; j != NULL; j = j->next) {
        run_job(j);
    }
}
