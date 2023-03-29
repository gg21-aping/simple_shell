#pragma once
#include "parse.h"

extern int shell_tty;
extern job *running_job;
extern job *stopped_job;
extern pid_t shell_pgid;
extern struct termios shell_tmodes;
extern int shell_terminal;
extern int shell_is_interactive;

void init_shell();
job *find_job(pid_t pgid);
void do_job_notification(void);
void sigchld_handler(int sig);
void wait_for_job(job *j);
void put_job_in_foreground(job *j, int cont);
void put_job_in_background(job *j, int cont);
void continue_job(job *j, int foreground);
int job_is_stopped(job *j);
void add_job(job **head, job *j);