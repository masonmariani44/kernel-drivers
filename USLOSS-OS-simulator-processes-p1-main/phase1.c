#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "phase1.h"
#include <usloss.h>

/*
Data structure stuff goes here
*/
typedef struct Process
{
    //linked list stuff
    struct Process *parent;
    struct Process *prev_sibling;
    struct Process *next_sibling;
    struct Process *child_head;

    //function data
    int (*func)(char *);
    char *arg;

    //metadata / uhh...data...?
    int pid;
    int run_state;      // == 0 is blocked, 1 active, < 0 dead
    int status;
    int priority;
    int stacksize;
    char *name;
    void *stack;

    unsigned int total_time;
    unsigned int slice_start_time;

    USLOSS_Context context;

} Process;



typedef struct Queue
{

    struct Queue *next;
    int pid;

} Queue;




//0 is init process and all other indicies correspond to pid processes[4] means pid of 4
Process *processes[50];


//used for priority queues
Queue *p1_head;
Queue *p1_tail;
Queue *p2_head;
Queue *p2_tail;
Queue *p3_head;
Queue *p3_tail;
Queue *p4_head;
Queue *p4_tail;
Queue *p5_head;
Queue *p5_tail;
Queue *p6_head;
Queue *p6_tail;
Queue *p7_head;
Queue *p7_tail;

Queue *priority_heads[8];
Queue *priority_tails[8];


Queue *zap_queue[50];


int join_block[50];

int blocked[50];

/*
Stored values go here
*/
int current_pid = 0;





/*
DEBUG
*/
void DumpPriority()
{
    USLOSS_Console("===DUMP ALERT===\n");
    for (int i = 0; i < 8; i++) {
        USLOSS_Console("p%d_head: ", i);
        Queue *cur = priority_heads[i];
        while (cur != NULL) {
            USLOSS_Console("| %d |", cur->pid);
            cur = cur->next;
        }
        USLOSS_Console("\n");
    }

}




/*
DISPATCHER
*/
void switchTo(int new_pid) {
    //unsigned int psr = USLOSS_PsrGet();
    //if (psr_helper() == 0) { return; }    

    //processes[current_pid]->run_state = 2;
    //processes[pid]->run_state = 1;

    USLOSS_Context *old = &(processes[current_pid]->context);
    USLOSS_Context *new = &(processes[new_pid]->context);

    if (processes[new_pid]->run_state == 0) { return; }

    //swapping new and old; updating time values
    Process *old_proc = processes[current_pid];
    old_proc->total_time = old_proc->total_time + (currentTime() - old_proc->slice_start_time);
    Process *new_proc = processes[new_pid];
    new_proc->slice_start_time = currentTime();

    current_pid = new_pid;
    USLOSS_ContextSwitch(old, new);

}



/*
Main dispatcher function
*/
void dispatcher()
{
    //DEBUG DumpPriority();
    int pid = 1;
    for (int p = 1; p <= 7; p++) {

        //if not null, pop head of this priority queue
        if (priority_heads[p] != NULL) {
            pid = priority_heads[p]->pid;

            //Queue *next = priority_heads[p]->next;
            //if (priority_heads[p]->next == NULL) {
            //    priority_heads[p] = NULL;
            //    priority_tails[p] = NULL;
            //} 
            //else {
            //    priority_heads[p] = next;
            //}
            //free(next);

            break;
            
        }

    }
    switchTo(pid);

}







/*
Helper Functions
*/
int check_kernel() {
    unsigned int psr = USLOSS_PsrGet();
    return (psr & 0x1);        // should return 0 if kernel is disabled, 1 if enabled
}

void disable_interrupts() {
    unsigned int psr = USLOSS_PsrGet();
    // if the current psr has interrupts enabled, disable them
    if (psr & 0x2 == 1) {
        psr = psr ^ 0x2;
        USLOSS_PsrSet(psr);
    }
}

int psr_helper() {
    unsigned int psr = USLOSS_PsrGet();
    if (check_kernel() == 0) { return 0; }
    disable_interrupts();
    return 1;
}

void change_modes() {
    unsigned int psr = USLOSS_PsrGet();
    if (psr & 0x1 == 1) { psr = psr ^ 0x1; }       // if kernel mode is enabled, disable it
    if (psr & 0x2 == 0) { psr = psr ^ 0x2; }       // if interrupts are disabled, enable them
    USLOSS_PsrSet(psr);
}


/*
Adds process to priority queue
*/
void QueueProcess(int pid) 
{

    int p = processes[pid]->priority;

    //create queue
    Queue *new_priority_queue = malloc(sizeof(Queue));
    new_priority_queue->next = NULL;
    new_priority_queue->pid = pid;

    //enqueue onto tail
    if (priority_heads[p] == NULL) {
        priority_heads[p] = new_priority_queue;
        priority_tails[p] = new_priority_queue;
    }
    else {
        priority_tails[p]->next = new_priority_queue;
        priority_tails[p] = new_priority_queue;
    }

}


/*
Removes a process from the priority queue
*/
void DequeueProcess()
{
    //USLOSS_Console("===DEBUG=== DequeueProcess(): PID: %d\n", current_pid);
    //DumpPriority();

    int p = processes[current_pid]->priority;
    Queue *cur = priority_heads[p];

    //case where there is just one object in queue
    if (cur->next == NULL) {
        if (cur->pid == current_pid) {
            priority_heads[p] = NULL;
            priority_tails[p] = NULL;
            free(cur);
        }
        return;
    }

    //search queue
    while (cur != NULL) {
        //match found, remove from queue

        if (cur->pid == current_pid) {
            priority_heads[p] = cur->next;
            free(cur);
            return;
        }

        if (cur->next->pid == current_pid) {
            Queue *temp_free = cur->next;
            cur->next = cur->next->next;
            free(temp_free);
            return;
        }
        cur = cur->next;
    }

}



/*
The trampoline function used to call the main function of a given process.
Changes PSR modes, calls the function, and cleans up when it returns.
*/
void Trampoline() {
    unsigned int psr = USLOSS_PsrGet();
    change_modes();

    int result = processes[current_pid]->func(processes[current_pid]->arg);

    USLOSS_PsrSet(psr);
    int temp_status;
    int join_result = 0;
    while (join_result != -2) {
        join_result = join(&temp_status);
    }
    quit(result);
}



int find_next_pid() {
    //im fully aware this SUCKS!!!
    //linear search for next open key. if there are none, returns -1.
    for (int i = 0; i < 50; i++) {
        if (processes[i] == NULL) {
            return i;
        }
    }
    return -1;
}



/*
Handles the internal logic for creating a process and saving it in the 
process table.
*/
int fork_save_process(char *name, int (*func)(char *), char *arg, int stacksize, int priority) {

    //find parent process and then make this process a child of it
    Process *parent_process = processes[current_pid];

    //setting next pointers to NULL
    Process *new_process = malloc(sizeof(Process));
    new_process->parent = parent_process;
    new_process->next_sibling = NULL;
    new_process->child_head = NULL;

    //adding new process to the head of the list
    //if there exists a child add to front, otherwise make head and set refrences to null
    Process *cur_child = parent_process->child_head;
    if (cur_child != NULL) {
        cur_child->prev_sibling = new_process;
        new_process->next_sibling = cur_child;
    }
    else {
        new_process->next_sibling = NULL;
    }
    new_process->prev_sibling = NULL;
    parent_process->child_head = new_process;

    //assigning function pointer and arg
    new_process->func = func;
    new_process->arg = arg;

    //assigning metadata section
    int new_pid = find_next_pid();
    new_process->pid = new_pid;
    new_process->run_state = 1;
    new_process->status = 1;
    new_process->priority = priority;
    new_process->stacksize = stacksize;
    new_process->name = name;
    void *stack = (void *)malloc(stacksize);
    new_process->stack = stack;

    new_process->total_time = 0;
    new_process->slice_start_time = 0;

    //assign context
    USLOSS_Context *context = malloc(sizeof(USLOSS_Context));
    USLOSS_ContextInit(context, stack, stacksize, NULL, Trampoline);
    new_process->context = *context;

    processes[new_pid] = new_process;

    return new_pid;

}



/*
Necessary Processes
*/

/*
Driver code for sentinel
*/
int sentinel_func(char *arg) {
    while (1==1) {
        if (phase2_check_io() == 0) {
            USLOSS_Trace("DEADLOCK DETECTED!  All of the processes have blocked, but I/O is not ongoing.\n");
            USLOSS_Halt(1);
        }
        USLOSS_WaitInt();
    }
}


/*
Driver code for testcase_main
*/
int testcase_func(char *arg) {

    int result = testcase_main();
    if (result != 0) {
        USLOSS_Trace("ERROR: Bad return from testcase_main process\n");
    }
    // TODO USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
    USLOSS_Halt(0);
}


/*
Driver code for init
*/
int init_func(char *arg) {

    //bootstrap
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    //call fork twice to create sentinel and testcase
    fork1("sentinel", sentinel_func, NULL, USLOSS_MIN_STACK, 7);
    fork1("testcase_main", testcase_func, NULL, USLOSS_MIN_STACK, 3);

    //USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using fork1() to create it.\n");
    //TEMP_switchTo(2);       //switching to testcase_main
    dispatcher();

    while (1==1) {
        int temp_status = processes[current_pid]->status;
        int result_pid = 1;
        result_pid = join(&temp_status);
        if (result_pid == -2) {
            USLOSS_Trace("ERROR: Bad return to init process\n");
            USLOSS_Halt(1);
        }
    }
}










/*
Graded functions
*/

/*
Initializes the values in the process array, blocking array, and priority queues
*/
void phase1_init() {
    unsigned int psr = USLOSS_PsrGet();
    if (psr_helper() == 0) { return; }

    //set all values in process and zap_queue arrays to NULL
    for (int i=0; i<50; i++) { processes[i] = NULL; }
    for (int i=0; i<50; i++) { zap_queue[i] = NULL; }
    for (int i=0; i<50; i++) { join_block[i] = -1; }
    for (int i=0; i<50; i++) { blocked[i] = 0; }

    //set values for priority queues
    p1_head = NULL;
    p1_tail = NULL;
    p2_head = NULL;
    p2_tail = NULL;
    p3_head = NULL;
    p3_tail = NULL;
    p4_head = NULL;
    p4_tail = NULL;
    p5_head = NULL;
    p5_tail = NULL;
    p6_head = NULL;
    p6_tail = NULL;
    p7_head = NULL;
    p7_tail = NULL;

    priority_heads[0] = NULL;
    priority_heads[1] = p1_head;
    priority_heads[2] = p2_head;
    priority_heads[3] = p3_head;
    priority_heads[4] = p4_head;
    priority_heads[5] = p5_head;
    priority_heads[6] = p6_head;
    priority_heads[7] = p7_head;

    priority_tails[0] = NULL;
    priority_tails[1] = p1_tail;
    priority_tails[2] = p2_tail;
    priority_tails[3] = p3_tail;
    priority_tails[4] = p4_tail;
    priority_tails[5] = p5_tail;
    priority_tails[6] = p6_tail;
    priority_tails[7] = p7_tail;

    USLOSS_IntVec[USLOSS_CLOCK_INT];

    USLOSS_PsrSet(psr);
}










/*
Creates the init process and then context switches to it.
*/
void startProcesses() {
    unsigned int psr = USLOSS_PsrGet();
    if (psr_helper() == 0) { return; }

    //setting next pointers to NULL
    Process *new_process = malloc(sizeof(Process));
    new_process->parent = NULL;
    new_process->next_sibling = NULL;
    new_process->child_head = NULL;
    new_process->prev_sibling = NULL;

    //assigning function pointer and arg
    new_process->func = init_func;
    new_process->arg = NULL;

    //assigning metadata section
    new_process->pid = 1;
    new_process->run_state = 1;
    new_process->status = 1;
    new_process->priority = 6;
    new_process->stacksize = USLOSS_MIN_STACK;
    new_process->name = "init";
    void *init_stack = malloc(USLOSS_MIN_STACK);
    new_process->stack = init_stack;

    new_process->total_time = 0;
    new_process->slice_start_time = 0;

    //assign context
    USLOSS_Context *init_context = malloc(sizeof(USLOSS_Context));
    USLOSS_ContextInit(init_context, init_stack, USLOSS_MIN_STACK, NULL, Trampoline);
    new_process->context = *init_context;

    processes[1] = new_process;
    QueueProcess(1);

    current_pid = 1;
    USLOSS_ContextSwitch(NULL, init_context);
    //TODO call dispatcher
    //dispatcher();

    USLOSS_PsrSet(psr);
}












/*
Creates a new process given the necessary paramaters. This new child processe's parent
will be the currently running process.
*/
int fork1(char *name, int (*func)(char *), char *arg, int stacksize, int priority) {

    unsigned int psr = USLOSS_PsrGet();
    if (psr_helper() == 0) { 
        USLOSS_Trace("ERROR: Someone attempted to call %s while in user mode!\n", __func__);
        USLOSS_Halt(1);
    }

    //checking args
    if ((strlen(name) > 50 || name == NULL) ||                            //check name is correct length and not null
    (priority > 5 && processes[0] != NULL && processes[1] != NULL) ||     //priority range check to include init and sentinel exceptions
    (priority > 7 || priority < 1) ||                                     //normal priority range check
    (func == NULL)) {                                                     //check func isnt NULL
        USLOSS_PsrSet(psr);
        return -1;
    }
    if (stacksize < USLOSS_MIN_STACK) {                                   //check if stacksize is above minimum
        USLOSS_PsrSet(psr);
        return -2;
    }
    
    //sets up the new process internally. if the returning pid is less than 0, something went wrong.
    int pid = fork_save_process(name, func, arg, stacksize, priority);
    if (pid < 0) {
        USLOSS_PsrSet(psr);
        return -1; 
    }

    //add to priority queue and call dispatcher
    QueueProcess(pid);
    dispatcher();

    //finish
    USLOSS_PsrSet(psr);
    return pid;
}











/*
Frees the process table of dead processes. Will reset head of linked list representation 
and rearrange sibling relationships. 
*/
int join(int *status) {

    unsigned int psr = USLOSS_PsrGet();
    if (psr_helper() == 0) { return; }

    int return_val = -2;        //by default means that it did not join?? i guess

    //check for a child with run state 0
    Process *parent_process = processes[current_pid];
    Process *cur_child = parent_process->child_head;
    if (cur_child == NULL) { return -2; }       //return -2 if the process has no children


    //check if any children are already dead
    int do_block = 1;
    while (cur_child != NULL) {
        if (cur_child->run_state < 0) {
            do_block = 0;
        }
        cur_child = cur_child->next_sibling;
    }
    if (do_block == 1) {
        blockMe(15);    //waiting for child to die
    }

    //search for dead child
    cur_child = parent_process->child_head;   
    while (cur_child != NULL) {
        if (cur_child->run_state < 0) {
            //if the dead child is the head, make its sibling the next head, (ok if null)
            if (cur_child == parent_process->child_head) {
                parent_process->child_head = cur_child->next_sibling;
            }

            //set index in process table to null, remove from linked list
            Process *loc_prev_sibling = cur_child->prev_sibling;
            Process *loc_next_sibling = cur_child->next_sibling;
            if (loc_prev_sibling != NULL) { loc_prev_sibling->next_sibling = loc_next_sibling; }
            if (loc_next_sibling != NULL) { loc_next_sibling->prev_sibling = loc_prev_sibling; }

            //set out pointer, and return value before freeing this child and cleaning up process table
            return_val = cur_child->pid;
            *status = cur_child->status;
            processes[return_val] = NULL;
            //free(cur_child->stack); //EDIT
            free(cur_child);
        }
        cur_child = cur_child->next_sibling;
    }

    USLOSS_PsrSet(psr);
    return return_val;
}











/*
Marks the currently running process as terminated. This process will then be able to
be cleaned up by join().
*/
void quit(int status) {

    unsigned int psr = USLOSS_PsrGet();
    if (psr_helper() == 0) { return; }
    
    if (processes[current_pid]->child_head == NULL) {
        //set status, make running state -1
        processes[current_pid]->run_state = -1;
        processes[current_pid]->status = status;
    }
    else {
        USLOSS_Trace("ERROR: Process pid %d called quit() while it still had children.\n", current_pid);
        USLOSS_Halt(1);
    }

    DequeueProcess();

    //reset zap_queue
    zap_queue[current_pid] = NULL;

    //unblock parent (probably) waiting on a join
    unblockProc(processes[current_pid]->parent->pid);

    //finish 
    dispatcher();
    USLOSS_PsrSet(psr);
}










/*
ZAPPING
*/

/*
Requests that a given process kill itself. Current process blocks and wait for this to happen
*/
void zap(int pid)
{
    unsigned int psr = USLOSS_PsrGet();
    if (psr_helper() == 0) { return; }



    //error checking
    if (pid == current_pid) {
        USLOSS_Console("ERROR: Attempt to zap() itself.\n");
        USLOSS_Halt(1);
    }
    if (pid == 1) {
        USLOSS_Console("ERROR: Attempt to zap() init.\n");
        USLOSS_Halt(1);        
    }
    if (pid < 0 || pid >= 50) {
        USLOSS_Console("ERROR: Attempt to zap() a non-existent process.\n");
        USLOSS_Halt(1);
    }
    if (processes[pid] == NULL) {
        USLOSS_Console("ERROR: Attempt to zap() a non-existent process.\n");
        USLOSS_Halt(1);        
    }
    if (processes[pid]->run_state < 0) {
        USLOSS_Console("ERROR: Attempt to zap() a process that is already in the process of dying.\n");
        USLOSS_Halt(1);        
    }

    //create new zap queue
    Queue *new_zap_queue = malloc(sizeof(Queue));
    new_zap_queue->next = NULL;
    new_zap_queue->pid = current_pid;

    //add to zap queue for given process
    Queue *cur = zap_queue[pid];
    if (cur == NULL) {
        zap_queue[pid] = new_zap_queue;
    }
    else {
        while (cur != NULL) {
            if (cur->next = NULL) {
                cur->next = new_zap_queue;
                break;
            }
            cur = cur->next;
        }
    }

    blockMe(11);

    USLOSS_PsrSet(psr);
}

/*
Checks if this process has been requested to kill itself. Wakes up requesting processes
*/
int isZapped() 
{
    //process is not zapped
    if (zap_queue[current_pid] == NULL) {
        return 0;
    }
    //process is zapped
    else {
        //iterate over this queue and free and wake up each blocked process
        Queue *cur = zap_queue[current_pid];
        Queue *temp;
        while(cur != NULL) {
            unblockProc(cur->pid);

            temp = cur;
            cur = cur->next;
            free(temp);
        }

        return 1;
    }
}










/*
PID
*/
int getpid() { return current_pid; }

/*
Prints a representation of the process table
*/
void dumpProcesses() {

    USLOSS_Console("%-20s %-7s %-7s %-10s %-8s %s\n", "Name", "PID", "PPID", "Priority", "Status", "State");
    for (int i=0; i<50; i++) {
        Process *cur = processes[i];

        int ppid = -1;
        char states[3][11] = {"Terminated", "Blocked", "Active"};

        if (cur != NULL) {
            if (cur->parent != NULL) { ppid = cur->parent->pid; }
            USLOSS_Console("%-20s %-7d %-7d %-10d %-8d %s\n", cur->name, cur->pid, ppid, cur->priority, cur->status, states[cur->run_state+1]);
        }
    }
}









/*
BLOCKING
*/

/*
Blocks the current process
*/
void blockMe(int block_status) 
{

    //set running state and status
    processes[current_pid]->run_state = 0;
    processes[current_pid]->status = block_status;

    //find this process in the priority queue and remove it
    DequeueProcess();

    //mark as blocked
    blocked[current_pid] = 1;

    //TODO call dispatcher
    dispatcher();

}


/*
Unblocks a blocked process given a pid
*/
int unblockProc(int pid) {
    
    Process *cur_process = processes[pid];

    //error checking
    if (cur_process == NULL || cur_process->run_state > 0 || cur_process->status <= 10 || blocked[pid] == 0) {
        return -2;
    }
    
    //complete, mark as runnable and queue for running
    cur_process->run_state = 1;
    QueueProcess(pid);

    //mark as unblocked
    blocked[current_pid] = 0;

    //TODO call dispatcher
    dispatcher();

    return 0;

}












/*
TIME, DOCTOR FREEMAN?
*/

/*
Returns the time this process started its current time slice
*/
int readCurStartTime() 
{
    return processes[current_pid]->slice_start_time;
}


/*
Code provided in spec

Returns the current time from DeviceInput
*/
int currentTime()
{
    int retval;
    int usloss_rc = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &retval);
    assert(usloss_rc == USLOSS_DEV_OK);
    return retval;
}



/*
Returns the total time a process has been running for
*/
int readtime() 
{
    return processes[current_pid]->total_time;
}


/*
Checks if current process has been running for 80ms, calls dispatcher if so
*/
void timeSlice() {

    //if time elapsed is at least 80 ms
    unsigned int elapsed_time = currentTime() - readCurStartTime();
    if (elapsed_time >= 80000) {
        dispatcher();
    }

}



/*
Code provided by spec
*/
static void clockHandler(int dev, void *arg)
{
    //if (debug) {
    //    USLOSS_Console("clockHandler(): PSR = %d\n", USLOSS_PsrGet());
    //    USLOSS_Console("clockHandler(): currentTime = %d\n", currentTime());
    //}

    /* make sure to call this first, before timeSlice(), since we want to do
    * the Phase 2 related work even if process(es) are chewing up lots of
    * CPU.
    */
    phase2_clockHandler();

    // call the dispatcher if the time slice has expired
    timeSlice();
    
    /* when we return from the handler, USLOSS automatically re-enables
    * interrupts and disables kernel mode (unless we were previously in
    * kernel code). Or I think so. I haven’t double-checked yet.
    */
}