#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <asm/unistd.h>
#include <assert.h>

#include "trace.h"

/* If the system headers did not provide the constants, hard-code the normal
   values.  */
#ifndef PTRACE_EVENT_FORK

#define PTRACE_OLDSETOPTIONS    21
#define PTRACE_SETOPTIONS       0x4200
#define PTRACE_GETEVENTMSG      0x4201

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD   0x00000001
#define PTRACE_O_TRACEFORK      0x00000002
#define PTRACE_O_TRACEVFORK     0x00000004
#define PTRACE_O_TRACECLONE     0x00000008
#define PTRACE_O_TRACEEXEC      0x00000010
#define PTRACE_O_TRACEVFORKDONE 0x00000020
#define PTRACE_O_TRACEEXIT      0x00000040

/* Wait extended result codes for the above trace options.  */
#define PTRACE_EVENT_FORK       1
#define PTRACE_EVENT_VFORK      2
#define PTRACE_EVENT_CLONE      3
#define PTRACE_EVENT_EXEC       4
#define PTRACE_EVENT_VFORK_DONE 5
#define PTRACE_EVENT_EXIT       6

#endif /* PTRACE_EVENT_FORK */

#ifdef ARCH_HAVE_UMOVELONG
extern int arch_umovelong (Process *, void *, long *, arg_type_info *);
int
umovelong (Process *proc, void *addr, long *result, arg_type_info *info) {
	return arch_umovelong (proc, addr, result, info);
}
#else
/* Read a single long from the process's memory address 'addr' */
int
umovelong (Process *proc, void *addr, long *result, arg_type_info *info) {
	long pointed_to;

	errno = 0;
	pointed_to = ptrace (PTRACE_PEEKTEXT, proc->pid, addr, 0);
	if (pointed_to == -1 && errno)
		return -errno;

	*result = pointed_to;
	if (info) {
		switch(info->type) {
			case ARGTYPE_INT:
				*result &= 0x00000000ffffffffUL;
			default:
				break;
		};
	}
	return 0;
}
#endif

void
trace_me(void) {
	debug(DEBUG_PROCESS, "trace_me: pid=%d", getpid());
	if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
		perror("PTRACE_TRACEME");
		exit(1);
	}
}

int
trace_pid(pid_t pid) {
	debug(DEBUG_PROCESS, "trace_pid: pid=%d", pid);
	if (ptrace(PTRACE_ATTACH, pid, 1, 0) < 0) {
		return -1;
	}

	/* man ptrace: PTRACE_ATTACH attaches to the process specified
	   in pid.  The child is sent a SIGSTOP, but will not
	   necessarily have stopped by the completion of this call;
	   use wait() to wait for the child to stop. */
	if (waitpid (pid, NULL, __WALL) != pid) {
		perror ("trace_pid: waitpid");
		return -1;
	}

	return 0;
}

void
trace_set_options(Process *proc, pid_t pid) {
	if (proc->tracesysgood & 0x80)
		return;

	debug(DEBUG_PROCESS, "trace_set_options: pid=%d", pid);

	long options = PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK |
		PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE |
		PTRACE_O_TRACEEXEC;
	if (ptrace(PTRACE_SETOPTIONS, pid, 0, options) < 0 &&
	    ptrace(PTRACE_OLDSETOPTIONS, pid, 0, options) < 0) {
		perror("PTRACE_SETOPTIONS");
		return;
	}
	proc->tracesysgood |= 0x80;
}

void
untrace_pid(pid_t pid) {
	debug(DEBUG_PROCESS, "untrace_pid: pid=%d", pid);
	ptrace(PTRACE_DETACH, pid, 1, 0);
}

void
continue_after_signal(pid_t pid, int signum) {
	debug(DEBUG_PROCESS, "continue_after_signal: pid=%d, signum=%d", pid, signum);
	ptrace(PTRACE_SYSCALL, pid, 0, signum);
}

static enum ecb_status
event_for_pid(Event * event, void * data)
{
	if (event->proc != NULL && event->proc->pid == (pid_t)(uintptr_t)data)
		return ecb_yield;
	return ecb_cont;
}

static int
have_events_for(pid_t pid)
{
	return each_qd_event(event_for_pid, (void *)(uintptr_t)pid) != NULL;
}

void
continue_process(pid_t pid)
{
	debug(DEBUG_PROCESS, "continue_process: pid=%d", pid);

	/* Only really continue the process if there are no events in
	   the queue for this process.  Otherwise just wait for the
	   other events to arrive.  */
	if (!have_events_for(pid))
		/* We always trace syscalls to control fork(),
		 * clone(), execve()... */
		ptrace(PTRACE_SYSCALL, pid, 0, 0);
	else
		debug(DEBUG_PROCESS,
		      "putting off the continue, events in que.");
}

/**
 * This is used for bookkeeping related to PIDs that the event
 * handlers work with.
 */
struct pid_task {
	pid_t pid;	/* This may be 0 for tasks that exited
			 * mid-handling.  */
	int sigstopped : 1;
	int got_event : 1;
	int delivered : 1;
	int vforked : 1;
	int sysret : 1;
} * pids;

struct pid_set {
	struct pid_task * tasks;
	size_t count;
	size_t alloc;
};

/**
 * Breakpoint re-enablement.  When we hit a breakpoint, we must
 * disable it, single-step, and re-enable it.  That single-step can be
 * done only by one task in a task group, while others are stopped,
 * otherwise the processes would race for who sees the breakpoint
 * disabled and who doesn't.  The following is to keep track of it
 * all.
 */
struct process_stopping_handler
{
	Event_Handler super;

	/* The task that is doing the re-enablement.  */
	Process * task_enabling_breakpoint;

	/* The pointer being re-enabled.  */
	Breakpoint * breakpoint_being_enabled;

	enum {
		/* We are waiting for everyone to land in t/T.  */
		psh_stopping = 0,

		/* We are doing the PTRACE_SINGLESTEP.  */
		psh_singlestep,

		/* We are waiting for all the SIGSTOPs to arrive so
		 * that we can sink them.  */
		psh_sinking,

		/* This is for tracking the ugly workaround.  */
		psh_ugly_workaround,
	} state;

	int exiting;

	struct pid_set pids;
};

static struct pid_task *
get_task_info(struct pid_set * pids, pid_t pid)
{
	assert(pid != 0);
	size_t i;
	for (i = 0; i < pids->count; ++i)
		if (pids->tasks[i].pid == pid)
			return &pids->tasks[i];

	return NULL;
}

static struct pid_task *
add_task_info(struct pid_set * pids, pid_t pid)
{
	if (pids->count == pids->alloc) {
		size_t ns = (2 * pids->alloc) ?: 4;
		struct pid_task * n = realloc(pids->tasks,
					      sizeof(*pids->tasks) * ns);
		if (n == NULL)
			return NULL;
		pids->tasks = n;
		pids->alloc = ns;
	}
	struct pid_task * task_info = &pids->tasks[pids->count++];
	memset(task_info, 0, sizeof(*task_info));
	task_info->pid = pid;
	return task_info;
}

static enum pcb_status
task_stopped(Process * task, void * data)
{
	enum process_status st = process_status(task->pid);
	if (data != NULL)
		*(enum process_status *)data = st;

	/* If the task is already stopped, don't worry about it.
	 * Likewise if it managed to become a zombie or terminate in
	 * the meantime.  This can happen when the whole thread group
	 * is terminating.  */
	switch (st) {
	case ps_invalid:
	case ps_tracing_stop:
	case ps_zombie:
	case ps_sleeping:
		return pcb_cont;
	case ps_stop:
	case ps_other:
		return pcb_stop;
	}

	abort ();
}

/* Task is blocked if it's stopped, or if it's a vfork parent.  */
static enum pcb_status
task_blocked(Process * task, void * data)
{
	struct pid_set * pids = data;
	struct pid_task * task_info = get_task_info(pids, task->pid);
	if (task_info != NULL
	    && task_info->vforked)
		return pcb_cont;

	return task_stopped(task, NULL);
}

static Event * process_vfork_on_event(Event_Handler * super, Event * event);

static enum pcb_status
task_vforked(Process * task, void * data)
{
	if (task->event_handler != NULL
	    && task->event_handler->on_event == &process_vfork_on_event)
		return pcb_stop;
	return pcb_cont;
}

static int
is_vfork_parent(Process * task)
{
	return each_task(task->leader, &task_vforked, NULL) != NULL;
}

static enum pcb_status
send_sigstop(Process * task, void * data)
{
	Process * leader = task->leader;
	struct pid_set * pids = data;

	/* Look for pre-existing task record, or add new.  */
	struct pid_task * task_info = get_task_info(pids, task->pid);
	if (task_info == NULL)
		task_info = add_task_info(pids, task->pid);
	if (task_info == NULL) {
		perror("send_sigstop: add_task_info");
		destroy_event_handler(leader);
		/* Signal failure upwards.  */
		return pcb_stop;
	}

	/* This task still has not been attached to.  It should be
	   stopped by the kernel.  */
	if (task->state == STATE_BEING_CREATED)
		return pcb_cont;

	/* Don't bother sending SIGSTOP if we are already stopped, or
	 * if we sent the SIGSTOP already, which happens when we are
	 * handling "onexit" and inherited the handler from breakpoint
	 * re-enablement.  */
	enum process_status st;
	if (task_stopped(task, &st) == pcb_cont)
		return pcb_cont;
	if (task_info->sigstopped) {
		if (!task_info->delivered)
			return pcb_cont;
		task_info->delivered = 0;
	}

	/* Also don't attempt to stop the process if it's a parent of
	 * vforked process.  We set up event handler specially to hint
	 * us.  In that case parent is in D state, which we use to
	 * weed out unnecessary looping.  */
	if (st == ps_sleeping
	    && is_vfork_parent (task)) {
		task_info->vforked = 1;
		return pcb_cont;
	}

	if (task_kill(task->pid, SIGSTOP) >= 0) {
		debug(DEBUG_PROCESS, "send SIGSTOP to %d", task->pid);
		task_info->sigstopped = 1;
	} else
		fprintf(stderr,
			"Warning: couldn't send SIGSTOP to %d\n", task->pid);

	return pcb_cont;
}

/* On certain kernels, detaching right after a singlestep causes the
   tracee to be killed with a SIGTRAP (that even though the singlestep
   was properly caught by waitpid.  The ugly workaround is to put a
   breakpoint where IP points and let the process continue.  After
   this the breakpoint can be retracted and the process detached.  */
static void
ugly_workaround(Process * proc)
{
	void * ip = get_instruction_pointer(proc);
	Breakpoint * sbp = dict_find_entry(proc->leader->breakpoints, ip);
	if (sbp != NULL)
		enable_breakpoint(proc, sbp);
	else
		insert_breakpoint(proc, ip, NULL, 1);
	ptrace(PTRACE_CONT, proc->pid, 0, 0);
}

static void
process_stopping_done(struct process_stopping_handler * self, Process * leader)
{
	debug(DEBUG_PROCESS, "process stopping done %d",
	      self->task_enabling_breakpoint->pid);
	size_t i;
	if (!self->exiting) {
		for (i = 0; i < self->pids.count; ++i)
			if (self->pids.tasks[i].pid != 0
			    && (self->pids.tasks[i].delivered
				|| self->pids.tasks[i].sysret))
				continue_process(self->pids.tasks[i].pid);
		continue_process(self->task_enabling_breakpoint->pid);
		destroy_event_handler(leader);
	} else {
		self->state = psh_ugly_workaround;
		ugly_workaround(self->task_enabling_breakpoint);
	}
}

/* Before we detach, we need to make sure that task's IP is on the
 * edge of an instruction.  So for tasks that have a breakpoint event
 * in the queue, we adjust the instruction pointer, just like
 * continue_after_breakpoint does.  */
static enum ecb_status
undo_breakpoint(Event * event, void * data)
{
	if (event != NULL
	    && event->proc->leader == data
	    && event->type == EVENT_BREAKPOINT)
		set_instruction_pointer(event->proc, event->e_un.brk_addr);
	return ecb_cont;
}

static enum pcb_status
untrace_task(Process * task, void * data)
{
	if (task != data)
		untrace_pid(task->pid);
	return pcb_cont;
}

static enum pcb_status
remove_task(Process * task, void * data)
{
	/* Don't untrace leader just yet.  */
	if (task != data)
		remove_process(task);
	return pcb_cont;
}

static void
detach_process(Process * leader)
{
	each_qd_event(&undo_breakpoint, leader);
	disable_all_breakpoints(leader);

	/* Now untrace the process, if it was attached to by -p.  */
	struct opt_p_t * it;
	for (it = opt_p; it != NULL; it = it->next) {
		Process * proc = pid2proc(it->pid);
		if (proc == NULL)
			continue;
		if (proc->leader == leader) {
			each_task(leader, &untrace_task, NULL);
			break;
		}
	}
	each_task(leader, &remove_task, leader);
	destroy_event_handler(leader);
	remove_task(leader, NULL);
}

static void
handle_stopping_event(struct pid_task * task_info, Event ** eventp)
{
	/* Mark all events, so that we know whom to SIGCONT later.  */
	if (task_info != NULL)
		task_info->got_event = 1;

	Event * event = *eventp;

	/* In every state, sink SIGSTOP events for tasks that it was
	 * sent to.  */
	if (task_info != NULL
	    && event->type == EVENT_SIGNAL
	    && event->e_un.signum == SIGSTOP) {
		debug(DEBUG_PROCESS, "SIGSTOP delivered to %d", task_info->pid);
		if (task_info->sigstopped
		    && !task_info->delivered) {
			task_info->delivered = 1;
			*eventp = NULL; // sink the event
		} else
			fprintf(stderr, "suspicious: %d got SIGSTOP, but "
				"sigstopped=%d and delivered=%d\n",
				task_info->pid, task_info->sigstopped,
				task_info->delivered);
	}
}

/* Some SIGSTOPs may have not been delivered to their respective tasks
 * yet.  They are still in the queue.  If we have seen an event for
 * that process, continue it, so that the SIGSTOP can be delivered and
 * caught by ltrace.  We don't mind that the process is after
 * breakpoint (and therefore potentially doesn't have aligned IP),
 * because the signal will be delivered without the process actually
 * starting.  */
static void
continue_for_sigstop_delivery(struct pid_set * pids)
{
	size_t i;
	for (i = 0; i < pids->count; ++i) {
		if (pids->tasks[i].pid != 0
		    && pids->tasks[i].sigstopped
		    && !pids->tasks[i].delivered
		    && pids->tasks[i].got_event) {
			debug(DEBUG_PROCESS, "continue %d for SIGSTOP delivery",
			      pids->tasks[i].pid);
			ptrace(PTRACE_SYSCALL, pids->tasks[i].pid, 0, 0);
		}
	}
}

static int
event_exit_p(Event * event)
{
	return event != NULL && (event->type == EVENT_EXIT
				 || event->type == EVENT_EXIT_SIGNAL);
}

static int
event_exit_or_none_p(Event * event)
{
	return event == NULL || event_exit_p(event)
		|| event->type == EVENT_NONE;
}

static int
await_sigstop_delivery(struct pid_set * pids, struct pid_task * task_info,
		       Event * event)
{
	/* If we still didn't get our SIGSTOP, continue the process
	 * and carry on.  */
	if (event != NULL && !event_exit_or_none_p(event)
	    && task_info != NULL && task_info->sigstopped) {
		debug(DEBUG_PROCESS, "continue %d for SIGSTOP delivery",
		      task_info->pid);
		/* We should get the signal the first thing
		 * after this, so it should be OK to continue
		 * even if we are over a breakpoint.  */
		ptrace(PTRACE_SYSCALL, task_info->pid, 0, 0);

	} else {
		/* If all SIGSTOPs were delivered, uninstall the
		 * handler and continue everyone.  */
		/* XXX I suspect that we should check tasks that are
		 * still around.  Is things are now, there should be a
		 * race between waiting for everyone to stop and one
		 * of the tasks exiting.  */
		int all_clear = 1;
		size_t i;
		for (i = 0; i < pids->count; ++i)
			if (pids->tasks[i].pid != 0
			    && pids->tasks[i].sigstopped
			    && !pids->tasks[i].delivered) {
				all_clear = 0;
				break;
			}
		return all_clear;
	}

	return 0;
}

static int
all_stops_accountable(struct pid_set * pids)
{
	size_t i;
	for (i = 0; i < pids->count; ++i)
		if (pids->tasks[i].pid != 0
		    && !pids->tasks[i].got_event
		    && !have_events_for(pids->tasks[i].pid))
			return 0;
	return 1;
}

static void
singlestep(Process * proc)
{
	debug(1, "PTRACE_SINGLESTEP");
	if (ptrace(PTRACE_SINGLESTEP, proc->pid, 0, 0))
		perror("PTRACE_SINGLESTEP");
}

/* This event handler is installed when we are in the process of
 * stopping the whole thread group to do the pointer re-enablement for
 * one of the threads.  We pump all events to the queue for later
 * processing while we wait for all the threads to stop.  When this
 * happens, we let the re-enablement thread to PTRACE_SINGLESTEP,
 * re-enable, and continue everyone.  */
static Event *
process_stopping_on_event(Event_Handler * super, Event * event)
{
	struct process_stopping_handler * self = (void *)super;
	Process * task = event->proc;
	Process * leader = task->leader;
	Breakpoint * sbp = self->breakpoint_being_enabled;
	Process * teb = self->task_enabling_breakpoint;

	debug(DEBUG_PROCESS,
	      "pid %d; event type %d; state %d",
	      task->pid, event->type, self->state);

	struct pid_task * task_info = get_task_info(&self->pids, task->pid);
	if (task_info == NULL)
		fprintf(stderr, "new task??? %d\n", task->pid);
	handle_stopping_event(task_info, &event);

	int state = self->state;
	int event_to_queue = !event_exit_or_none_p(event);

	/* Deactivate the entry if the task exits.  */
	if (event_exit_p(event) && task_info != NULL)
		task_info->pid = 0;

	/* Always handle sysrets.  Whether sysret occurred and what
	 * sys it rets from may need to be determined based on process
	 * stack, so we need to keep that in sync with reality.  Note
	 * that we don't continue the process after the sysret is
	 * handled.  See continue_after_syscall.  */
	if (event != NULL && event->type == EVENT_SYSRET) {
		debug(1, "%d LT_EV_SYSRET", event->proc->pid);
		event_to_queue = 0;
		task_info->sysret = 1;
	}

	switch (state) {
	case psh_stopping:
		/* If everyone is stopped, singlestep.  */
		if (each_task(leader, &task_blocked, &self->pids) == NULL) {
			debug(DEBUG_PROCESS, "all stopped, now SINGLESTEP %d",
			      teb->pid);
			if (sbp->enabled)
				disable_breakpoint(teb, sbp);
			singlestep(teb);
			self->state = state = psh_singlestep;
		}
		break;

	case psh_singlestep:
		/* In singlestep state, breakpoint signifies that we
		 * have now stepped, and can re-enable the breakpoint.  */
		if (event != NULL && task == teb) {

			/* This is not the singlestep that we are waiting for.  */
			if (event->type == EVENT_SIGNAL) {
				singlestep(task);
				break;
			}

			/* Essentially we don't care what event caused
			 * the thread to stop.  We can do the
			 * re-enablement now.  */
			if (sbp->enabled)
				enable_breakpoint(teb, sbp);

			continue_for_sigstop_delivery(&self->pids);

			self->breakpoint_being_enabled = NULL;
			self->state = state = psh_sinking;

			if (event->type == EVENT_BREAKPOINT)
				event = NULL; // handled
		} else
			break;

		/* fall-through */

	case psh_sinking:
		if (await_sigstop_delivery(&self->pids, task_info, event))
			process_stopping_done(self, leader);
		break;

	case psh_ugly_workaround:
		if (event == NULL)
			break;
		if (event->type == EVENT_BREAKPOINT) {
			undo_breakpoint(event, leader);
			if (task == teb)
				self->task_enabling_breakpoint = NULL;
		}
		if (self->task_enabling_breakpoint == NULL
		    && all_stops_accountable(&self->pids)) {
			undo_breakpoint(event, leader);
			detach_process(leader);
			event = NULL; // handled
		}
	}

	if (event != NULL && event_to_queue) {
		enque_event(event);
		event = NULL; // sink the event
	}

	return event;
}

static void
process_stopping_destroy(Event_Handler * super)
{
	struct process_stopping_handler * self = (void *)super;
	free(self->pids.tasks);
}

void
continue_after_breakpoint(Process *proc, Breakpoint *sbp)
{
	set_instruction_pointer(proc, sbp->addr);
	if (sbp->enabled == 0) {
		continue_process(proc->pid);
	} else {
		debug(DEBUG_PROCESS,
		      "continue_after_breakpoint: pid=%d, addr=%p",
		      proc->pid, sbp->addr);
#if defined __sparc__  || defined __ia64___ || defined __mips__
		/* we don't want to singlestep here */
		continue_process(proc->pid);
#else
		struct process_stopping_handler * handler
			= calloc(sizeof(*handler), 1);
		if (handler == NULL) {
			perror("malloc breakpoint disable handler");
		fatal:
			/* Carry on not bothering to re-enable.  */
			continue_process(proc->pid);
			return;
		}

		handler->super.on_event = process_stopping_on_event;
		handler->super.destroy = process_stopping_destroy;
		handler->task_enabling_breakpoint = proc;
		handler->breakpoint_being_enabled = sbp;
		install_event_handler(proc->leader, &handler->super);

		if (each_task(proc->leader, &send_sigstop,
			      &handler->pids) != NULL)
			goto fatal;

		/* And deliver the first fake event, in case all the
		 * conditions are already fulfilled.  */
		Event ev;
		ev.type = EVENT_NONE;
		ev.proc = proc;
		process_stopping_on_event(&handler->super, &ev);
#endif
	}
}

/**
 * Ltrace exit.  When we are about to exit, we have to go through all
 * the processes, stop them all, remove all the breakpoints, and then
 * detach the processes that we attached to using -p.  If we left the
 * other tasks running, they might hit stray return breakpoints and
 * produce artifacts, so we better stop everyone, even if it's a bit
 * of extra work.
 */
struct ltrace_exiting_handler
{
	Event_Handler super;
	struct pid_set pids;
};

static Event *
ltrace_exiting_on_event(Event_Handler * super, Event * event)
{
	struct ltrace_exiting_handler * self = (void *)super;
	Process * task = event->proc;
	Process * leader = task->leader;

	debug(DEBUG_PROCESS, "pid %d; event type %d", task->pid, event->type);

	struct pid_task * task_info = get_task_info(&self->pids, task->pid);
	handle_stopping_event(task_info, &event);

	if (event != NULL && event->type == EVENT_BREAKPOINT)
		undo_breakpoint(event, leader);

	if (await_sigstop_delivery(&self->pids, task_info, event)
	    && all_stops_accountable(&self->pids))
		detach_process(leader);

	/* Sink all non-exit events.  We are about to exit, so we
	 * don't bother with queuing them. */
	if (event_exit_or_none_p(event))
		return event;

	return NULL;
}

static void
ltrace_exiting_destroy(Event_Handler * super)
{
	struct ltrace_exiting_handler * self = (void *)super;
	free(self->pids.tasks);
}

static int
ltrace_exiting_install_handler(Process * proc)
{
	/* Only install to leader.  */
	if (proc->leader != proc)
		return 0;

	/* Perhaps we are already installed, if the user passed
	 * several -p options that are tasks of one process.  */
	if (proc->event_handler != NULL
	    && proc->event_handler->on_event == &ltrace_exiting_on_event)
		return 0;

	/* If stopping handler is already present, let it do the
	 * work.  */
	if (proc->event_handler != NULL) {
		assert(proc->event_handler->on_event
		       == &process_stopping_on_event);
		struct process_stopping_handler * other
			= (void *)proc->event_handler;
		other->exiting = 1;
		return 0;
	}

	struct ltrace_exiting_handler * handler
		= calloc(sizeof(*handler), 1);
	if (handler == NULL) {
		perror("malloc exiting handler");
	fatal:
		/* XXXXXXXXXXXXXXXXXXX fixme */
		return -1;
	}

	handler->super.on_event = ltrace_exiting_on_event;
	handler->super.destroy = ltrace_exiting_destroy;
	install_event_handler(proc->leader, &handler->super);

	if (each_task(proc->leader, &send_sigstop,
		      &handler->pids) != NULL)
		goto fatal;

	return 0;
}

/*
 * When the traced process vforks, it's suspended until the child
 * process calls _exit or exec*.  In the meantime, the two share the
 * address space.
 *
 * The child process should only ever call _exit or exec*, but we
 * can't count on that (it's not the role of ltrace to policy, but to
 * observe).  In any case, we will _at least_ have to deal with
 * removal of vfork return breakpoint (which we have to smuggle back
 * in, so that the parent can see it, too), and introduction of exec*
 * return breakpoint.  Since we already have both breakpoint actions
 * to deal with, we might as well support it all.
 *
 * The gist is that we pretend that the child is in a thread group
 * with its parent, and handle it as a multi-threaded case, with the
 * exception that we know that the parent is blocked, and don't
 * attempt to stop it.  When the child execs, we undo the setup.
 */

struct process_vfork_handler
{
	Event_Handler super;
	void * bp_addr;
};

static Event *
process_vfork_on_event(Event_Handler * super, Event * event)
{
	struct process_vfork_handler * self = (void *)super;
	Breakpoint * sbp;
	assert(self != NULL);

	switch (event->type) {
	case EVENT_BREAKPOINT:
		/* Remember the vfork return breakpoint.  */
		if (self->bp_addr == NULL)
			self->bp_addr = event->e_un.brk_addr;
		break;

	case EVENT_EXIT:
	case EVENT_EXIT_SIGNAL:
	case EVENT_EXEC:
		/* Smuggle back in the vfork return breakpoint, so
		 * that our parent can trip over it once again.  */
		if (self->bp_addr != NULL) {
			sbp = dict_find_entry(event->proc->leader->breakpoints,
					      self->bp_addr);
			if (sbp != NULL)
				insert_breakpoint(event->proc->parent,
						  self->bp_addr,
						  sbp->libsym, 1);
		}

		continue_process(event->proc->parent->pid);

		/* Remove the leader that we artificially set up
		 * earlier.  */
		change_process_leader(event->proc, event->proc);
		destroy_event_handler(event->proc);

	default:
		;
	}

	return event;
}

void
continue_after_vfork(Process * proc)
{
	debug(DEBUG_PROCESS, "continue_after_vfork: pid=%d", proc->pid);
	struct process_vfork_handler * handler = calloc(sizeof(*handler), 1);
	if (handler == NULL) {
		perror("malloc vfork handler");
		/* Carry on not bothering to treat the process as
		 * necessary.  */
		continue_process(proc->parent->pid);
		return;
	}

	/* We must set up custom event handler, so that we see
	 * exec/exit events for the task itself.  */
	handler->super.on_event = process_vfork_on_event;
	install_event_handler(proc, &handler->super);

	/* Make sure that the child is sole thread.  */
	assert(proc->leader == proc);
	assert(proc->next == NULL || proc->next->leader != proc);

	/* Make sure that the child's parent is properly set up.  */
	assert(proc->parent != NULL);
	assert(proc->parent->leader != NULL);

	change_process_leader(proc, proc->parent->leader);
}

static int
is_mid_stopping(Process *proc)
{
	return proc != NULL
		&& proc->event_handler != NULL
		&& proc->event_handler->on_event == &process_stopping_on_event;
}

void
continue_after_syscall(Process * proc, int sysnum, int ret_p)
{
	/* Don't continue if we are mid-stopping.  */
	if (ret_p && (is_mid_stopping(proc) || is_mid_stopping(proc->leader))) {
		debug(DEBUG_PROCESS,
		      "continue_after_syscall: don't continue %d",
		      proc->pid);
		return;
	}
	continue_process(proc->pid);
}

/* If ltrace gets SIGINT, the processes directly or indirectly run by
 * ltrace get it too.  We just have to wait long enough for the signal
 * to be delivered and the process terminated, which we notice and
 * exit ltrace, too.  So there's not much we need to do there.  We
 * want to keep tracing those processes as usual, in case they just
 * SIG_IGN the SIGINT to do their shutdown etc.
 *
 * For processes ran on the background, we want to install an exit
 * handler that stops all the threads, removes all breakpoints, and
 * detaches.
 */
void
ltrace_exiting(void)
{
	struct opt_p_t * it;
	for (it = opt_p; it != NULL; it = it->next) {
		Process * proc = pid2proc(it->pid);
		if (proc == NULL || proc->leader == NULL)
			continue;
		if (ltrace_exiting_install_handler(proc->leader) < 0)
			fprintf(stderr,
				"Couldn't install exiting handler for %d.\n",
				proc->pid);
	}
}

size_t
umovebytes(Process *proc, void *addr, void *laddr, size_t len) {

	union {
		long a;
		char c[sizeof(long)];
	} a;
	int started = 0;
	size_t offset = 0, bytes_read = 0;

	while (offset < len) {
		a.a = ptrace(PTRACE_PEEKTEXT, proc->pid, addr + offset, 0);
		if (a.a == -1 && errno) {
			if (started && errno == EIO)
				return bytes_read;
			else
				return -1;
		}
		started = 1;

		if (len - offset >= sizeof(long)) {
			memcpy(laddr + offset, &a.c[0], sizeof(long));
			bytes_read += sizeof(long);
		}
		else {
			memcpy(laddr + offset, &a.c[0], len - offset);
			bytes_read += (len - offset);
		}
		offset += sizeof(long);
	}

	return bytes_read;
}

/* Read a series of bytes starting at the process's memory address
   'addr' and continuing until a NUL ('\0') is seen or 'len' bytes
   have been read.
*/
int
umovestr(Process *proc, void *addr, int len, void *laddr) {
	union {
		long a;
		char c[sizeof(long)];
	} a;
	unsigned i;
	int offset = 0;

	while (offset < len) {
		a.a = ptrace(PTRACE_PEEKTEXT, proc->pid, addr + offset, 0);
		for (i = 0; i < sizeof(long); i++) {
			if (a.c[i] && offset + (signed)i < len) {
				*(char *)(laddr + offset + i) = a.c[i];
			} else {
				*(char *)(laddr + offset + i) = '\0';
				return offset + (signed)i;
			}
		}
		offset += sizeof(long);
	}
	*(char *)(laddr + offset) = '\0';
	return offset;
}

size_t
uunmovebytes(Process *proc, void *addr, void *laddr, size_t len) {
	union {
		long a;
		char c[sizeof(long)];
	} a;
	int started = 0;
	size_t offset = 0, bytes_written = 0;

	while (offset < len) {
		if (len - offset >= sizeof(long)) {
			memcpy(&a.c[0], laddr + offset, sizeof(long));
		}
		else {
			a.a = ptrace(PTRACE_PEEKTEXT, proc->pid, addr + offset, 0);
			if (a.a == -1 && errno) {
				if (started && errno == EIO)
					return bytes_written;
				else
					return -1;
			}
			memcpy(&a.c[0], laddr + offset, len - offset);
		}

		a.a = ptrace(PTRACE_POKETEXT, proc->pid, addr + offset, a.a);
		if (a.a == -1 && errno) {
			if (started && errno == EIO)
				return bytes_written;
			else
				return -1;
		}

		if (len - offset >= sizeof(long))
			bytes_written += sizeof(long);
		else
			bytes_written += len - offset;

		started = 1;

		offset += sizeof(long);
	}

	return bytes_written;
}
