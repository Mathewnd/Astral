#ifndef _JOBCTL_H
#define _JOBCTL_H

#include <kernel/scheduler.h>

int jobctl_newsession(proc_t *proc);
int jobctl_newgroup(proc_t *proc);
int jobctl_changegroup(proc_t *proc, proc_t *group);
void jobctl_detach(proc_t *);
void jobctl_addproc(proc_t *parent, proc_t *proc);
void jobctl_procremove(proc_t *proc);
pid_t jobctl_getpgid(proc_t *proc);
pid_t jobctl_getsid(proc_t *proc);
int jobctl_setctty(proc_t *proc, void *ctty, bool steal);
void *jobctl_getctty(proc_t *proc);
int jobctl_signal(proc_t *proc, int signal);
proc_t *jobctl_getforeground(proc_t *proc);
int jobctl_setforeground(proc_t *session, proc_t *pgrp);

#endif
