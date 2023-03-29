#include "job.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

job *running_job = NULL;
job *stopped_job = NULL;
pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

void sigchld_handler(int sig) { do_job_notification(); }

void init_shell() {
    // check interaction
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        // loop til forground
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        // ignore interaction and job-control signals
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        // signal(SIGCHLD, SIG_IGN);
        signal(SIGCHLD, sigchld_handler);

        // to our own process group
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Couldn't put the shell in its own process group");
            exit(EXIT_FAILURE);
        }

        // get control of the terminal
        tcsetpgrp(shell_terminal, shell_pgid);

        // save default terminal attributes for shell
        tcgetattr(shell_terminal, &shell_tmodes);
    }
}

job *find_job(pid_t pgid) {
    job *j;

    for (j = running_job; j; j = j->next)
        if (j->pgid == pgid)
            return j;
    return NULL;
}

int job_is_stopped(job *j) {
    process *p;

    for (p = j->process_list; p; p = p->next)
        if (!p->completed && !p->stopped)
            return 0;
    return 1;
}

int job_is_completed(job *j) {
    process *p;

    for (p = j->process_list; p; p = p->next)
        if (!p->completed)
            return 0;
    return 1;
}

void delete_job(job **head, job *j) {
    job **pp_j = head;
    for (; *pp_j; pp_j = &(*pp_j)->next)
        if (*pp_j == j) {
            *pp_j = (*pp_j)->next;
            free_job(j);
            break;
        }
}
void remove_job(job **head, job *j) {
    job **pp_j = head;
    for (; *pp_j; pp_j = &(*pp_j)->next) {
        if (*pp_j == j) {
            *pp_j = (*pp_j)->next;
            j->next = NULL;
            return;
        }
    }
}

void put_job_in_foreground(job *j, int cont) {
    // put the job to foreground
    tcsetpgrp(shell_terminal, j->pgid);

    // send continue signal to the job if necessary
    if (cont) {
        tcsetattr(shell_terminal, TCSADRAIN, &j->tmodes);
        if (kill(-j->pgid, SIGCONT) < 0)
            perror("kill (SIGCONT)");
    }

    // wait
    wait_for_job(j);
    if (job_is_completed(j)) {
        delete_job(&running_job, j);
    } else if (job_is_stopped(j)) {
        remove_job(&running_job, j);
        add_job(&stopped_job, j);
    }

    // put the shell back to foreground
    tcsetpgrp(shell_terminal, shell_pgid);

    // restore terminal mode
    tcgetattr(shell_terminal, &j->tmodes);
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
}

void put_job_in_background(job *j, int cont) {
    // send continue signal to the job if necessary
    if (cont)
        if (kill(-j->pgid, SIGCONT) < 0)
            perror("kill (SIGCONT)");
}

// store the status of the process pid that was returned by waitpid
// return 0 if all went well, nonzero otherwise

int mark_process_status(pid_t pid, int status) {
    job *j;
    process *p;

    if (pid > 0) {
        // update process record
        for (j = running_job; j; j = j->next) {
            for (p = j->process_list; p; p = p->next)
                if (p->pid == pid) {
                    p->status = status;
                    if (WIFSTOPPED(status))
                        p->stopped = 1;
                    else {
                        p->completed = 1;
                        if (WIFSIGNALED(status))
                            fprintf(stderr, "%d: Terminated by signal %d.\n", (int)pid,
                                    WTERMSIG(p->status));
                    }
                    return 0;
                }
        }
        fprintf(stderr, "No child process %d.\n", pid);
        return -1;
    } else if (pid == 0 || errno == ECHILD)
        // no process to report
        return -1;
    else {
        // other errors
        perror("waitpid");
        return -1;
    }
}

// check for processes that have status information available, without blocking.

void update_status(void) {
    int status;
    pid_t pid;

    do
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
    while (!mark_process_status(pid, status));
}

// check for processes that have status information available,
// blocking until all processes in the given job have reported.

void wait_for_job(job *j) {
    int status;
    pid_t pid;
    do
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);
    while (!mark_process_status(pid, status) && !job_is_stopped(j) &&
           !job_is_completed(j));
}

// format information about job status

void format_job_info(job *j, const char *status) {
    fprintf(stderr, "%ld (%s): %s\n", (long)j->pgid, status, j->command);
}

// notify the user about stopped or terminated jobs.
// delete terminated jobs from the active job list.  */

void do_job_notification(void) {
    job *j, *jlast, *jnext;

    // update status information for child processes
    update_status();

    jlast = NULL;
    for (j = running_job; j; j = jnext) {
        jnext = j->next;

        // notify user all job is completed and delete jobs from the active jobs list
        if (job_is_completed(j)) {
            if (jlast)
                jlast->next = jnext;
            else
                running_job = jnext;
            free_job(j);
        }

        // mark and notify stopped jobs -> avoid repeating processing
        else if (job_is_stopped(j) && !j->notified) {
            j->notified = 1;
            jlast = j;
        }

        // for running processes
        else
            jlast = j;
    }
}

// mark a stopped job J as being running again

void mark_job_as_running(job *j) {
    process *p;

    for (p = j->process_list; p; p = p->next)
        p->stopped = 0;
    j->notified = 0;
}

// continue the job J

void continue_job(job *j, int foreground) {
    if (!j) {
        return;
    }
    mark_job_as_running(j);
    remove_job(&stopped_job, j);
    add_job(&running_job, j);
    if (foreground) {
        put_job_in_foreground(j, 1);
    } else
        put_job_in_background(j, 1);
}

void add_job(job **head, job *j) {
    j->next = *head;
    *head = j;
}
