#include "job.h"

void command_fg() { continue_job(stopped_job, 1); }

void command_bg() { continue_job(stopped_job, 0); }