#pragma once

struct command
{
    const char *name;
    void (*func) (void);
};

void command_fg();
void command_bg();
