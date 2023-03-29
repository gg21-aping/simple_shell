#pragma once
#include "parse.h"

extern char **environ;

void run_job(job *j);
void run_jobs(job *j);