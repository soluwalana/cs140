#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "lib/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* The value passed to recalculate priority
   In order to prevent it from moving a thread
   from one ready queue to another ready queue*/
#define NO_SWITCH ((void*)1)

/* A list of blocked threads which are blocked
   on a certain tick to transpire*/
static struct list sleep_list;

static struct lock sleep_list_lock;

/* Queues used by the multi level feedback
   queue scheduler */
static struct list mlfqs_queue[PRI_MAX+1];

/* Variable to track the load avg of the system*/
static fixed_point load_avg;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Prototypes for defined functions in this file that were changed
   in the threads assignment*/
int count_ready_threads (void);
void count_thread_if_ready(struct thread *t, void *count);
void recalculate_priority(struct thread *t, void *switchQueues);
void recalculate_recent_cpu (struct thread *t, void *none UNUSED);
static void mlfqs_init(void);
static void mlfqs_insert(struct thread *t);
static void mlfqs_switch_queue(struct thread *t, int new_priority);
static struct thread *mlfqs_get_next_thread_to_run(void);

static void release_locks(void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init (void){
	ASSERT (intr_get_level () == INTR_OFF);
	lock_init (&tid_lock);
	lock_init(&sleep_list_lock);

	list_init (&ready_list);
	list_init (&all_list);
	
	list_init (&sleep_list);

	/* init the mlfqs */
	if(thread_mlfqs){
		mlfqs_init();
		load_avg = 0;
	}

	/* Set up a thread structure for the running thread.
	   We are now running in the current thread. */
	initial_thread = running_thread ();

	init_thread (initial_thread, "main", PRI_DEFAULT);

	/* Set the default value for the fields used by the mlfqs*/
	initial_thread->recent_cpu = 0;
	initial_thread->nice = 0;

	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid (); /* Gives the main thread as 1 */


}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start (void){
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread.
	   This will block main thread. Then call schedule */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function RUNS IN AN EXTERNAL INTERRUPT CONTEXT. */
void thread_tick (void){
	struct thread *t = thread_current ();

	/* Update statistics. */
	if(t == idle_thread){
		idle_ticks++;
	}
#ifdef USERPROG
	else if(t->pagedir != NULL){
		user_ticks++;
	}
#endif
	else {
		kernel_ticks++;
	}

	if( thread_mlfqs ){
		/* Increase recent cpu of active thread on every tick */
		t->recent_cpu = fp_add(t->recent_cpu, itof(1));
	}

	/* Enforce preemption. */
	if(++thread_ticks >= TIME_SLICE){
		intr_yield_on_return ();
	}
}

/* Prints thread statistics. */
void thread_print_stats (void){
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		    idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create (const char *name, int priority,
                     thread_func *function, void *aux){

	if(thread_mlfqs) priority = PRI_MAX;

	struct thread *t;
	struct kernel_thread_frame *kf;
	struct switch_entry_frame *ef;
	struct switch_threads_frame *sf;
	tid_t tid;
	enum intr_level old_level;

	ASSERT (function != NULL);

	/* Allocate thread. Each of which has a limited stack size of one page.
	   Palloc_get_page allocates whole pages of memory*/
	t = palloc_get_page (PAL_ZERO);
	if(t == NULL){
		return TID_ERROR;
	}
	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Prepare thread for first run by initializing its stack.
	   Do this atomically so intermediate values for the 'stack'
	   member cannot be observed. */
	old_level = intr_disable ();

#ifdef USERPROG
	/* Initialize the user process */
	struct process *p = calloc (1, sizeof(struct process));
	if(p == NULL){
		intr_set_level (old_level);
		return TID_ERROR;
	}

	if( initialize_process (p, t) == false){
		free(p);
		intr_set_level (old_level);
		return TID_ERROR;
	}
#endif
	/* Stack frame for kernel_thread(). */
	kf = alloc_frame (t, sizeof *kf);
	kf->eip = NULL;
	kf->function = function;
	kf->aux = aux;

	/* Stack frame for switch_entry(). */
	ef = alloc_frame (t, sizeof *ef);
	ef->eip = (void (*) (void)) kernel_thread;

	/* Stack frame for switch_threads(). */
	sf = alloc_frame (t, sizeof *sf);
	sf->eip = switch_entry;
	sf->ebp = 0;

	intr_set_level (old_level);

	/* Add to a run queue. */
	thread_unblock (t);

	/* This thread may be the highest so we need to see
	 if preemption is necessary from the current thread
	 to the new thread */
	thread_preempt();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block (void){
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock (struct thread *t){
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	if(!thread_mlfqs){
		list_push_back (&ready_list, &t->elem);
	}else{
		mlfqs_insert(t);
	}
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *thread_name (void){
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *thread_current (void){
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid (void){
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit (void){
	ASSERT (!intr_context ());
	intr_disable();
	
	/* Remove thread from all threads list, set our status to dying,
	   and schedule another process.  That process will destroy us
	   when it calls thread_schedule_tail(). */
	list_remove (&thread_current()->allelem);

#ifdef USERPROG
	process_exit ();
#endif

	release_locks();
	thread_current ()->status = THREAD_DYING;
	schedule ();
	NOT_REACHED ();
}

/* For all of the held locks, release without preempting */
static void release_locks(){
	ASSERT(!intr_context());
	struct thread *t = thread_current();
	while(!list_empty(&t->held_locks)){
		struct list_elem *e = list_pop_front(&t->held_locks);
		struct lock *lock = list_entry(e, struct lock, elem);
		lock_release_preempt(lock, false);
	}
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield (void){
	struct thread *cur = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();

	/* Put in a queue if it isn't idle*/
	if(cur != idle_thread){
		if(!thread_mlfqs){
			/*Put this thread into the ready queue*/
			list_push_back (&ready_list, &cur->elem);
		}else{
			/*Put this thread into its appropriate
			  priority queue*/
			mlfqs_insert(cur);
		}
	}

	cur->status = THREAD_READY;
	schedule ();
	intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach (thread_action_func *func, void *aux){
	struct list_elem *e;

	ASSERT (intr_get_level () == INTR_OFF);

	for(e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
		struct thread *t = list_entry (e, struct thread, allelem);
		func (t, aux);
	}
}

/* Sets the current thread's priority to 'new_priority'.
   Then updates any threads that this thread is
   waiting on and if necessary preempts the running thread because
   it's priority may be lower or higher now than another different
   thread
   Thread priority setting is not changeable in mlfqs mode
   Use thread_set_nice instead*/
void thread_set_priority (int new_priority){
	if(thread_mlfqs){
		return;
	}

	struct thread *t = thread_current ();
	t->priority = new_priority;
	t->tmp_priority = new_priority;

	/* Make sure that our tmp_priority is the max of all the
	   threads waiting on one of our locks and the currently
	   updated priority. */
	enum intr_level old_level = intr_disable ();
	update_temp_priority(t);
	intr_set_level (old_level);

	/* We may no longer be the highest priority thread*/
	thread_preempt();
}

/* Returns the current thread's priority. */
int thread_get_priority (void){
	struct thread *t = thread_current ();
	/* if mlfqs return actual priority*/
	if(thread_mlfqs){
		return t->priority;
	}
	return t->tmp_priority;
}

/* Sets the current thread's nice value to NICE.
   This function is not defined when NOT running
   mlfqs. Use thread_set_priority in that case*/
void thread_set_nice (int nice){
	if(!thread_mlfqs){
		return;
	}

	/* set nice value*/
	struct thread *t = thread_current ();
	t->nice = nice;

	/* recompute priority */
	recalculate_priority(t, NO_SWITCH);

	/* check if thread is still the highest if not
	   then yield the cpu and go to the back of
	   the correct priority queue*/
	thread_preempt();
}

/* Returns the current thread's nice value.
   Undefined value if not running mlfqs*/
int thread_get_nice (void){
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg (void){
	return ftoi(fp_mult(itof(100),load_avg));
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu (void){
	fixed_point fpCPU = fp_mult(running_thread()->recent_cpu, itof(100));
	return ftoi(fpCPU);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle (void *idle_started_ UNUSED){
	struct semaphore *idle_started = idle_started_;
	idle_thread = thread_current ();
	sema_up (idle_started);

	while(1){
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.
		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   portant; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread (thread_func *function, void *aux){
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *running_thread (void){
	uint32_t *esp;

	/* Copy the CPU's stack pointer into `esp', and then round that
	   down to the start of a page.  Because `struct thread' is
	   always at the beginning of a page and the stack pointer is
	   somewhere in the middle, this locates the current thread.
	   THE ASSUMPTION IS THAT KERNEL IS USING ONLY ONE PAGE FOR
	   STACK FOR EACH THREAD */

	asm ("mov %%esp, %0" : "=g" (esp));
	return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool is_thread (struct thread *t){
	return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread (struct thread *t, const char *name, int priority){
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->stack = (uint8_t *) t + PGSIZE;
	t->priority = priority;

	t->lock_waited_on = NULL;
	t->magic = THREAD_MAGIC;

	if(thread_mlfqs){
		struct thread *running = running_thread();
		t->recent_cpu = running->recent_cpu;
		t->nice = running->nice;
	}else{
		t->tmp_priority = priority;
	}

	list_init (&t->held_locks);

	list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *alloc_frame (struct thread *t, size_t size){
	/* Stack data is always allocated in word-size units. */
	ASSERT (is_thread (t));
	ASSERT (size % sizeof (uint32_t) == 0);

	t->stack -= size;
	return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *next_thread_to_run (void){
	if(!thread_mlfqs){
		if(list_empty (&ready_list)){
			return idle_thread;
		}else{
			/*Select the item off the queue with the highest priority*/
			struct list_elem *e =
					remove_list_max(&ready_list, &thread_hash_compare);
			ASSERT(e != NULL);
			struct thread *t =  list_entry(e, struct thread,elem);
			ASSERT(is_thread(t));
			ASSERT(t->status == THREAD_READY);
			return t;
		}
	}else{
		return mlfqs_get_next_thread_to_run();
	}
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void thread_schedule_tail (struct thread *prev){
	struct thread *cur = running_thread ();
	/* Mark us as running. */
	cur->status = THREAD_RUNNING;
#ifdef USERPROG
	/* Activate the new address space. */
	process_activate ();
#endif
	/* prev and cur can't be the same and dying or we will
	 * reach Non-reachable code as a thread that is dying
	 * now is running and will try to resume execution*/
	ASSERT (prev != cur && cur ->status != THREAD_DYING);
	ASSERT (intr_get_level () == INTR_OFF);

	/* Start new time slice. */
	thread_ticks = 0;

	/* If the thread we switched from is dying, destroy its struct
	   thread.  This must happen late so that thread_exit() doesn't
	   pull out the rug under itself.  (We don't free
	   initial_thread because its memory was not obtained via
	   palloc().) */
	if(prev != NULL && prev->status == THREAD_DYING && prev != initial_thread){
		ASSERT (prev != cur);
		palloc_free_page (prev);
	}
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void schedule (void){
	struct thread *cur = running_thread ();
	struct thread *next = next_thread_to_run ();
	struct thread *prev = NULL;

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (cur->status != THREAD_RUNNING);
	ASSERT (is_thread (next));


	if(cur != next){
		prev = switch_threads (cur, next);
	}
	thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid (void){
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

bool thread_is_alive(tid_t tid){
	struct list_elem *e;
	for(e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
		struct thread *t = list_entry(e, struct thread, allelem);
		if(t->tid == tid){
			return true;
		}
	}
	return false;
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* This function takes as a parameter the current tick of the system.
   It iterates through the list of sleeping threads and checks to see
   if that threads wake up time is less than or equal to the current tick.
   If so it will unblock the thread, putting it on the ready list.
   This should be only run from the interrupt timer! */
void thread_check_sleeping(int64_t current_tick){
	ASSERT (intr_context ());
	struct list_elem *e;
	if(list_begin(&sleep_list) != list_end(&sleep_list)){
		for(e = list_begin(&sleep_list); e != list_end(&sleep_list);){
			struct thread *t = list_entry(e, struct thread, elem);
			if(t->wake_time <= current_tick){

				/* This needs to happen first because
				   thread_unblock moves e to the ready list
				   leaving the sleep list in an inconsistent state
				   if e isn't removed first*/
				e = list_remove(e);

				thread_unblock(t);
				continue;
			}

			e = list_next(e);
		}
	}

}

/* Puts the thread to sleep, and starts a new thread on the ready
   list Takes as a parameter the wake_time which is the system ticks
   Plus the desired number of ticks to sleep. Thread must be running. */
void thread_sleep(int64_t wake_time){
	struct thread *cur = running_thread ();
	ASSERT(is_thread(cur));
	ASSERT(cur->status == THREAD_RUNNING);

	/*The time that the thread should wake up*/
	cur->wake_time = wake_time;

	enum intr_level old_level = intr_disable();
	list_push_back(&sleep_list, &cur->elem);
	thread_block();
	intr_set_level(old_level);
}

/* Determines if this thread is the highest priority thread
   to be running and if it isn't it immediately yeilds
   This will cause the scheduler to choose the highest priority
   thread to run and everything will be great!
   Does nothing if this thread is the highest priority */
void thread_preempt(void){
	struct thread *cur = running_thread ();
	ASSERT(is_thread(cur));
	ASSERT(cur->status == THREAD_RUNNING);

	/* Needs to be run with intr_disabled because
	   it interacts with the ready_list and with
	   the tmp_priorities. These are global data
	   susceptible to race conditions. And the mlfqs
	   Will just check to see if the thread has the
	   highest priority if not it just yields*/
	enum intr_level old_level = intr_disable();
	if(!thread_mlfqs){
		if(!list_empty(&ready_list)){
			struct thread *tHigh = list_entry(
					list_max(&ready_list, &thread_hash_compare, NULL),
					struct thread, elem);
			if(tHigh->tmp_priority > cur->tmp_priority){
				thread_yield();
			}
		}
	}else if(mlfqs_get_highest_priority() > cur->priority){
		thread_yield();
	}

	intr_set_level (old_level);
}

/* Recalculates the load avg using the formula
   (59/60)*load_avg + (1/60)*(#running/ready threads)*/
void recalculate_loads (void){
	load_avg = fp_add( fp_mult(fp_div(itof(59),itof(60)), (load_avg) ),
					   fp_div( itof(count_ready_threads()), itof(60)) );
}

/* Returns the number of threads that are ready or running
   Right now. Uses the thread_foreach function */
int count_ready_threads (){
	int count = 0;
	thread_foreach(&count_thread_if_ready, &count);
	return count;
}

/*Increments count if the thread passed to it was running or ready*/
void count_thread_if_ready(struct thread *t, void *count){
	if(t != idle_thread &&
			(t->status == THREAD_RUNNING || t->status == THREAD_READY)){
		(*((int*)count)) ++;
	}
}

/* returns the number of running or ready threads*/
void recalculate_priorities (void){
	thread_foreach(&recalculate_priority, NULL);
}

/* Calculates thread priority = PRI_MAX - (recent_cpu/4)-(nice*2)
   and if switchQueues == NULL then it will move the thread to the
   correct ready queue if its status is READY*/
void recalculate_priority(struct thread *t, void *switchQueues){
	if(t == idle_thread){
		return;
	}

	fixed_point newP =
			fp_sub(itof(PRI_MAX), fp_int_div(t->recent_cpu,4));
	newP = fp_sub(newP, itof(t->nice*2));
	int newPriority = ftoi(newP);

	/* Bounds newPriority*/
	if(newPriority < PRI_MIN){
		newPriority = PRI_MIN;
	}else if(newPriority > PRI_MAX){
		newPriority = PRI_MAX;
	}

	if(switchQueues == NULL){
		mlfqs_switch_queue(t,newPriority);
	}
}

/* Iterates over all of the threads and updates their cpu usage
   stats*/
void recalculate_all_recent_cpu (void){
	thread_foreach(&recalculate_recent_cpu, NULL);
}

/* Calculates, and assigns the recent_cpu field of the thread
   calculates the cpu usage according to:
   recent_cpu = (2*loadavg)/(2*loadavg+1) * recent_cpu + nice */
void recalculate_recent_cpu (struct thread *t, void *none UNUSED){
	fixed_point enumer = fp_int_mult(load_avg,2);
	fixed_point coefficient =fp_div(enumer,fp_int_add(enumer, 1));
	t->recent_cpu = fp_int_add(fp_mult(coefficient, t->recent_cpu), t->nice);
}

/* This function takes as parameters list_elem *a, which is a memeber of a
   thread and list_elem *b which is a member of a thread and return true
   if thread A has priority LESS than that of thread b */
bool thread_hash_compare (const struct list_elem *a,
					const struct list_elem *b,
					void *aux UNUSED){
	ASSERT(a != NULL);
	ASSERT(b != NULL);
	if(!thread_mlfqs){
		return ((list_entry(a, struct thread, elem)->tmp_priority) <
				(list_entry(b, struct thread, elem)->tmp_priority));
	}else{
		return ((list_entry(a, struct thread, elem)->priority) <
				(list_entry(b, struct thread, elem)->priority));
	}
}

/* Initializes the mlfqs system */
static void mlfqs_init(void){
	int i = 0;
	for(; i < PRI_MAX+1; i++){
		list_init(&mlfqs_queue[i]);
	}
}

/* Inserts the thread into the mlfqs queue based on its priority. */
static void mlfqs_insert(struct thread *t){
	ASSERT(is_thread(t));
	ASSERT(t->priority >= PRI_MIN && t->priority <= PRI_MAX);
	list_push_back(&mlfqs_queue[t->priority], &t->elem);
}


/*Returns the highest priority (int) in the mflqs
  System. This is defined as the first thread in the
  highest priority bucket */
int mlfqs_get_highest_priority(void){
	int i = PRI_MAX;
	for(; i >= 0; i--){
		if(!list_empty(&mlfqs_queue[i])){
			return i;
		}
	}
	return 0; /* we should only get here when there is one thread */
}

/* Updates the priority and if the thread is in a ready queu
   switches the indicated ready thread to the mlfqs queue for the
   new_priority. Does nothing if the new_priority is the same
   (Preserves position in its ready queue if it is waiting) */
static void mlfqs_switch_queue(struct thread *t, int new_priority){
	if(new_priority == t->priority) return;
	t->priority = new_priority;

	if( t->status == THREAD_READY){
		list_remove(&t->elem);
		mlfqs_insert(t);
	}
}

/* Returns the next thread to be scheduled as determined
   by mlfqs or idle_thread if there is no thread to be run
   yet. This will be defined as the first thread in the
   highest priority bucket.
   This function removes the thread from any mlfqs priority
   bucket that it was in. */
static struct thread *mlfqs_get_next_thread_to_run(void){
	int i = PRI_MAX;
	for(; i >= 0; i--){
		if(!list_empty(&mlfqs_queue[i])){
			struct list_elem *e = list_pop_front(&mlfqs_queue[i]);
			struct thread *next= list_entry(e, struct thread, elem);
			ASSERT (is_thread(next));
			ASSERT (next->status == THREAD_READY);
			return next;
		}
	}
	return idle_thread;
}
