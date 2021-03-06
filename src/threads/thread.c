#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

static int FP;

static int inttoreal(int x){
    return x * FP;
}

static int realtoint(const real* x){
    if (x->val >= 0) {
        return (x->val + (FP/2))/FP;
    } else {
        return (x->val - (FP/2))/FP;
    }
}

static int add(const real* x, const real* y){
    return x->val + y->val;
}

static int subtract(const real* x, const real* y){
    return x->val - y->val;
}

static int multiply(const real* x, const real* y){
    return ((int64_t)x->val * y->val) /FP;
}

static int divide(const real* x, const real* y){
    return ((int64_t)x->val) * FP / y->val;
}

static bool thread_started;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Array of list of processes in THREAD_READY state to enable mlfqs */
static struct list ready_list_mlfqs[PRI_MAX-PRI_MIN+1];

/* List of processes run during last second */
static struct list last_tslice_list;

/* List of processes put to sleep */
static struct list sleep_list;

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
struct kernel_thread_frame 
  {
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

static real load_avg;  /* load_avg as real number */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static int init_thread (struct thread *, const char *name, int priority);
static int init_thread_control_block (struct thread *, bool);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

static list_less_func priority_less;
bool priority_less(const struct list_elem *elem1,
              const struct list_elem *elem2, void *aux UNUSED){
    /* ready list is sorted by this function and we want thread of highest priority to be at front of the list */
    return list_entry (elem1, struct thread, elem)->priority > list_entry (elem2, struct thread, elem)->priority;
}

static thread_action_func update_recent_cpu;
void update_recent_cpu (struct thread *t, void *aux UNUSED){

    real f1, f2, ratio;
    f1.val = inttoreal(2);
    f2.val = inttoreal(1);
    
    f1.val = multiply(&f1, &load_avg);
    f2.val = add(&f1, &f2);
    ratio.val = divide(&f1, &f2);
    
    t->recent_cpu.val = multiply(&ratio, &t->recent_cpu);
    t->recent_cpu.val = add(&t->recent_cpu, &t->nice);
    
}

/* calculate mlfsq priority for single thread */
void calculate_mlfqs_thread_priority(struct thread* t, void *aux UNUSED)
{
    real f1, f2, priority;
    f1.val = inttoreal(PRI_MAX);
    f2.val = inttoreal(4);
    
    f2.val = divide(&t->recent_cpu, &f2);
    f1.val = subtract(&f1, &f2);
    
    f2.val = inttoreal(2);
    f2.val = multiply(&t->nice, &f2);
    
    priority.val = subtract(&f1, &f2);
    t->priority = realtoint(&priority);
    
    if (t->priority > PRI_MAX) t->priority = PRI_MAX;
    if (t->priority < PRI_MIN) t->priority = PRI_MIN;
}

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
void
thread_init (void) 
{
  
  ASSERT (intr_get_level () == INTR_OFF);

  thread_started = false;
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);
  list_init (&last_tslice_list);
    
  if (thread_mlfqs){
      for (int i = 0; i < PRI_MAX+1; i++) list_init(&ready_list_mlfqs[i]);
      load_avg.val = inttoreal(0);
      FP = 1<<DQ;
    }
    
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  thread_started = true;
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);

  #ifdef USERPROG
  ASSERT (init_thread_control_block(thread_current(), false) != -1);
  #endif
    
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
 
    if (t != idle_thread) {
        real incre;
        incre.val = inttoreal(1);
        t->recent_cpu.val = add(&t->recent_cpu, &incre);
    }
    
  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;
  
  int64_t t_ticks = timer_ticks();
  int prev_priority = t->priority;
  
  /* update priority from last run list */
  if (thread_mlfqs && t_ticks % TIME_SLICE == 0 ) {
      ASSERT(!list_empty(&last_tslice_list));
      update_last_run_mlfqs_priority_and_queue();
      clear_lastrun_list();
    }

  /* update load avg, recent_cpu and priority for all threads */
  if (thread_mlfqs && t_ticks % TIMER_FREQ == 0)
      update_mlfqs_parameters();
  
  /* wake up sleeping threads */
  wakeup_threads();
    
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE || t->priority < prev_priority)
    intr_yield_on_return ();
}


/* every time slice, remove every thread in last run list except current running thread */
void
clear_lastrun_list(void){
    
    if (!list_empty(&last_tslice_list)) {
        struct list_elem* e = list_front(&last_tslice_list);
        while(e != list_tail(&last_tslice_list)) {
            struct thread *t = list_entry(e, struct thread, lastrun_elem);
            if (t != thread_current() ){
                e = list_remove(e);
                t->lastrun_elem.prev = NULL;
                t->lastrun_elem.next = NULL;
            } else {
                e = list_next(e);
            }
        }
            
    }
}


/* update threads priority related parameters for all threads*/
void
update_mlfqs_parameters(void)
{
    /* update recent_cpu for all threads */
    thread_foreach(&update_recent_cpu, NULL);

    /* update load avg */
    struct thread *t = thread_current ();
    int num_ready_threads = 0;
    if (t != idle_thread) num_ready_threads++;
    
    for (int i = PRI_MIN; i<PRI_MAX+1; i++){
        num_ready_threads += list_size(&ready_list_mlfqs[i]);
    }
    
    
    real f1, f2;
    f1.val = inttoreal(59); f2.val =inttoreal(60);
    f1.val = divide(&f1, &f2);
    
    load_avg.val = multiply(&f1, &load_avg);
    
    f1.val = inttoreal(1);
    f2.val = divide(&f1, &f2);
    f1.val = inttoreal(num_ready_threads);

    f1.val = multiply(&f2, &f1);
    
    load_avg.val = add(&load_avg, &f1);

    /* update priority */
    struct list_elem *e;

    for (e = list_begin (&all_list); e != list_end (&all_list);
         e = list_next (e))
      {
        struct thread *t = list_entry (e, struct thread, allelem);
        int prev_priority = t->priority;
        calculate_mlfqs_thread_priority(t, NULL);
        if (t->status == THREAD_READY && t->priority != prev_priority ) upadte_thread_mlfqs_ready_list(t);
      }
    
}


/* update mlfqs priority for threads run from last time slice */
void
update_last_run_mlfqs_priority_and_queue(void)
{
    enum intr_level old_level;
    old_level = intr_disable ();
    
    struct list_elem *e;

    for (e = list_begin (&last_tslice_list); e != list_end (&last_tslice_list);
         e = list_next (e))
      {
        struct thread *t = list_entry (e, struct thread, lastrun_elem);
        int prev_priority = t->priority;
        calculate_mlfqs_thread_priority(t, NULL);
        if (t->status == THREAD_READY && t->priority != prev_priority ) upadte_thread_mlfqs_ready_list(t);
      }
    intr_set_level (old_level);
}

/* reassgin thread to ready list based on probably updated priority if the thread is not curretn running thread */
void
upadte_thread_mlfqs_ready_list(struct thread *t)
{
    if (t == thread_current()) return;
    ASSERT(t->priority >= PRI_MIN && t->priority <= PRI_MAX);
    list_remove(&t->elem);
    list_push_back(&ready_list_mlfqs[t->priority], &t->elem);
}


/* Prints thread statistics. */
void
thread_print_stats (void) 
{
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
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
    
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  if (init_thread (t, name, priority) == -1) return TID_ERROR;
  tid = t->tid = allocate_tid ();
  t->tcb->tid = tid;
    
#ifdef FILESYS
  t->pwd = thread_current()->pwd;
#endif
    
  #ifdef VM
  t->vm_mm = vm_mm_init();
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
  
  /* Add to run queue. */
  thread_unblock (t);
      
  if (t->priority > thread_current()->priority)  thread_yield();
    
  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
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
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  if (!thread_mlfqs) {
      list_insert_ordered(&ready_list, &t->elem, &priority_less, NULL);
  } else {
      list_push_back(&ready_list_mlfqs[t->priority], &t->elem);
  }

  t->status = THREAD_READY;
  intr_set_level (old_level);
    
}

/* put thread to sleep */
void
thread_sleep(int64_t sleep_start_time, int64_t duration)
{
    if (duration == 0) return;
    struct thread *cur = thread_current ();
    
    enum intr_level old_level;
    
    ASSERT (!intr_context ());

    old_level = intr_disable ();
    
    ASSERT(cur->status != THREAD_SLEEPING);
    cur->status = THREAD_SLEEPING;
    cur->sleep_start_time = sleep_start_time;
    cur->sleep_time = duration;
    
//    ASSERT (intr_get_level () == INTR_ON);
    list_push_back(&sleep_list, &cur->elem);
   
    schedule ();
    intr_set_level (old_level);
    
}

/* wake up sleeping thread. intterupt must be disabled. */
void thread_wakeup (struct thread* t)
{
    ASSERT (t->status == THREAD_SLEEPING);
    ASSERT (is_thread (t));
    
    t->status = THREAD_READY;
    if (!thread_mlfqs) {
        list_insert_ordered(&ready_list, &t->elem, &priority_less, NULL);
    } else {
        list_push_back(&ready_list_mlfqs[t->priority], &t->elem);
    }
    
}

/* examine elapsed time for sleeping threads and wake up
 the thread if time's up */
void wakeup_threads(void)
{
    enum intr_level old_level;

    old_level = intr_disable ();
    
    struct list_elem *e = list_begin (&sleep_list);
    while ( e != list_end (&sleep_list) )
      {
        struct thread *t = list_entry (e, struct thread, elem);
        ASSERT(t->sleep_start_time);
        ASSERT(t->sleep_time);
          if (timer_elapsed (t->sleep_start_time) >= t->sleep_time) {
              e = list_remove (e);
              thread_wakeup(t);
          } else {
              e = list_next (e);
          }
      }
    intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
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
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());
    
#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
    
  /* remove exiting threads from last_run list */
  if (thread_mlfqs && thread_current()->lastrun_elem.prev != NULL && thread_current()->lastrun_elem.next != NULL ) list_remove (&thread_current()->lastrun_elem);
  thread_current ()->status = THREAD_DYING;

  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  if (!thread_started) return;
    
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread ) {
      if (!thread_mlfqs) {
        list_insert_ordered(&ready_list, &cur->elem, &priority_less, NULL);
      } else {
          list_push_back(&ready_list_mlfqs[cur->priority], &cur->elem);
      }
    }
    
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}


/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs) return;
    
  struct thread* curr = thread_current();
  curr->init_priority = new_priority;
  
    if (!list_empty(&curr->thread_wait_list)) {
        if (new_priority > curr->priority) curr->priority = new_priority;
    } else {
    thread_current()->priority = new_priority;
    }
      
  if (!list_empty (&ready_list) &&
      new_priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) thread_yield();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice)
{
  struct thread* curr = thread_current();
  curr->nice.val = inttoreal(nice);
    
  int prev_priority = curr->priority;
  calculate_mlfqs_thread_priority(curr, NULL);
  if (curr->priority < prev_priority) thread_yield();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return realtoint(&thread_current()->nice);
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
    real f1;
    f1.val = inttoreal(100);
    f1.val = multiply(&load_avg, &f1);
    return realtoint(&f1);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
    real f1;
    f1.val = inttoreal(100);
    f1.val = multiply(&thread_current()->recent_cpu, &f1);
    return realtoint(&f1);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Initialize thread control block. It's only available after
 paging has been initialized */
static int
init_thread_control_block(struct thread *t, bool setup_parent)
{
    list_init(&t->child_list);
    list_init(&t->fildes);
    
    /* initialize file descriptor number to 2 */
    t->fd_no = 2;
#ifdef VM
    t->mmap_no = 0;
    list_init(&t->mmap_list);
#endif
    
    t->tcb = malloc (sizeof(struct thread_control_block));
    
    if (t->tcb == NULL) return -1;
    
    t->tcb->file = NULL;
    /* init sema for sync */
    sema_init(&t->tcb->sema, setup_parent ? 0 : 1);
      
    /* establish parent/child relatioship */
    t->tcb->parent_exit = false;
    t->tcb->thread_exit = false;
    t->tcb->loaded = false;
    t->tcb->exit_status = 0;
    
    t->tcb->tid = t->tid;
    if (setup_parent && strcmp(t->name, "idle") != 0) {
        struct thread* parent_thread = thread_current();
        t->tcb->parent_td = parent_thread;
        list_push_back(&parent_thread->child_list, &t->tcb->elem);
    }
    
    return 0;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static int
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->recent_cpu.val = inttoreal(0);
  t->nice.val = inttoreal(0);
    
  if (!thread_mlfqs) {
      t->priority = priority;
      t->init_priority = priority;
  } else {
      calculate_mlfqs_thread_priority(t, NULL);
  }
    
  #ifdef USERPROG
  if (strcmp(name, "main") != 0) {
      if (init_thread_control_block(t, true) == -1) {
          palloc_free_page(t);
          return -1;
      }
  }
  #endif
        
  t->magic = THREAD_MAGIC;
  list_init(&t->thread_wait_list);
    
  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
    
  intr_set_level (old_level);
    
  return 0;
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
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
static struct thread *
next_thread_to_run (void) 
{
    if (!thread_mlfqs){
        if (list_empty (&ready_list))
          return idle_thread;
        else
          return list_entry (list_pop_front (&ready_list), struct thread, elem);
    } else {
        
        int pri_level = PRI_MAX;
        while (pri_level >= PRI_MIN && list_empty(&ready_list_mlfqs[pri_level]) ) pri_level--;
        if (pri_level < PRI_MIN) return idle_thread;
        return list_entry (list_pop_front (&ready_list_mlfqs[pri_level]),
                           struct thread, elem);
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
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  if (thread_started) process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
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
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  /* insert next thread into last_run list if it's not already in the list */
  if (thread_mlfqs) {
     if (is_thread(cur) && cur->lastrun_elem.prev == NULL && cur->lastrun_elem.next == NULL )
         list_push_back(&last_tslice_list, &cur->lastrun_elem);
     if (is_thread(next) && next->lastrun_elem.prev == NULL && next->lastrun_elem.next == NULL )
        list_push_back(&last_tslice_list, &next->lastrun_elem);
   }
    
   if (cur != next) {
       prev = switch_threads (cur, next);
    }
    
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Allocate file descriptor for a new file. */
int
allocate_fd (struct file* fp)
{
  struct file_descriptor *fd;
//  fd = palloc_get_page(PAL_ZERO);
  fd = malloc(sizeof(struct file_descriptor));
  
  if (fd == NULL) return -1;
  
  struct thread *cur = thread_current();
    
  fd->fp = fp;
  lock_acquire (&tid_lock);
  fd->fd_no = cur->fd_no++;
  list_push_back(&cur->fildes, &fd->elem);
  lock_release (&tid_lock);
    
  return fd->fd_no;
        
}

/* Allocate id for a new mmap file. */
int
allocate_mmapid (void *start_pg, void *end_pg)
{
  struct mmap_descriptor *mmap_d = malloc(sizeof(struct mmap_descriptor));
  
  if (mmap_d == NULL) return -1;
  
  struct thread *cur = thread_current();
    
  mmap_d->start_pg = start_pg;
  mmap_d->end_pg = end_pg;
  lock_acquire (&tid_lock);
  mmap_d->mmap_no = cur->mmap_no++;
  list_push_back(&cur->mmap_list, &mmap_d->elem);
  lock_release (&tid_lock);
    
  return mmap_d->mmap_no;
        
}

struct mmap_descriptor*
fetch_mmap(int mmap_no)
{
    struct thread *cur = thread_current();
    struct mmap_descriptor *mmap_d;
    
    if (!list_empty(&cur->mmap_list)) {
        struct list_elem *e = list_front(&cur->mmap_list);
        while (e != list_tail(&cur->mmap_list)) {
            mmap_d = list_entry(e, struct mmap_descriptor, elem);
            if (mmap_d->mmap_no == mmap_no) break;
            e = list_next(e);
        }
        return e != list_tail(&cur->mmap_list) ? mmap_d : NULL;
    } else {
        return NULL;
    }
    
}

/* Fetch file descriptor pointer for a fd no. */
struct file_descriptor*
fetch_file_descriptor(int fd_no)
{
    struct thread *cur = thread_current();
    struct file_descriptor *fd;
    
    if (!list_empty(&cur->fildes)) {
        struct list_elem *e = list_front(&cur->fildes);
        while (e != list_tail(&cur->fildes)) {
            fd = list_entry(e, struct file_descriptor, elem);
            if (fd->fd_no == fd_no) break;
            e = list_next(e);
        }
        return e != list_tail(&cur->fildes) ? fd : NULL;
    } else {
        return NULL;
    }
            
};


/* Fetch file pointer for a fd no. */
struct file*
fetch_file(int fd_no)
{
    struct file_descriptor *fd = fetch_file_descriptor(fd_no);
    return fd == NULL ? NULL : fd->fp;
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
