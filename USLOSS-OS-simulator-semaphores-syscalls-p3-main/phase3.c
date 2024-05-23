#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include "phase3.h"
#include <stdio.h>
#include <stdlib.h>


//int (*trampoline_func)(char *);       //EDIT: creating spawnqueue instead
//char *trampoline_arg;

typedef struct SpawnQueue
{
    int (*trampoline_func)(char *);     //pointer to function that will be run with spawn
    char *name;                         //name of function (for debugging)
    struct SpawnQueue *next;            //pointer to the next queued function
} SpawnQueue;
SpawnQueue *head_SpawnQueue;            //stores a pointer to the head of the SpawnQueue
SpawnQueue *tail_SpawnQueue;            //stores a pointer to the tail of the SpawnQueue

//cant use array name "semaphores" because testcase uses it :(
int val_semaphores[MAXSEMS];            //stores the value of the semaphore at the semaphore's id (val_semaphore[sem_id] = sem_value)
int blocked_semaphores[MAXSEMS];        //stores the mboxid of the blocked semaphore at the location of the semaphore's id (blocked_semahpores[sem_id] = mbox_id)

/*
DEBUGGING
*/
void dumpSpawnQueue()
{
    USLOSS_Console("--------------------DUMPING SpawnQueue--------------------\n");
    SpawnQueue *cur = head_SpawnQueue;
    while (cur != NULL) {
        USLOSS_Console("| %s |", cur->name);
        cur = cur->next;
    }
    USLOSS_Console("\n------------------END DUMPING SpawnQueue------------------\n");
}

void dumpSemaphores()
{
    USLOSS_Console("--------------------DUMPING SEMAPHORES--------------------\n");
    for (int i=0; i<MAXSEMS; i++) {
        if (val_semaphores[i] != -1) {
            USLOSS_Console("| %d |", val_semaphores[i]);
        }
    }
    USLOSS_Console("\n------------------END DUMPING SEMAPHORES------------------\n");
}

void dumpBlockedSemaphores()
{
    USLOSS_Console("--------------------DUMPING BLOCKED SEMAPHORES--------------------\n");
    for (int i=0; i<MAXSEMS; i++) {
        if (blocked_semaphores[i] != -1) {
            USLOSS_Console("| %d |", blocked_semaphores[i]);
        }
    }
    USLOSS_Console("\n------------------END DUMPING BLOCKED SEMAPHORES------------------\n");
}


/*
GENERAL HELPERS
*/

/*
Linearly searches for next avialable semaphore in the val_semaphore
array. This is the array that stores the semaphore value at the index
that represents its id. (val_semaphore[sem_id] = sem_value)
*/
int find_next_sem()
{
    for (int i=0; i<MAXSEMS; i++) {
        if (val_semaphores[i] == -1) {
            return i;
        }
    }
    return -1;
}

/*
Disables kernel mode, duh
*/
void disable_kernel()
{
    unsigned int psr = USLOSS_PsrGet();
    if (psr & 1 == 1) {
        USLOSS_PsrSet(psr ^ 1);
    }
}

/*
Switches to user mode, dequeues the head of the SpawnQueue and runs its 
respective function, then terminates if the function returns
*/
int Trampoline(char *arg) {
    disable_kernel();

    //dequeue spawnqueue head (cur_func is what will be called)
    int (*cur_func)(char *) = head_SpawnQueue->trampoline_func;
    if (head_SpawnQueue->next == NULL) {
        free(head_SpawnQueue);
        head_SpawnQueue = NULL;
        tail_SpawnQueue = NULL;
    }
    else {
        SpawnQueue *temp_head_SpawnQueue = head_SpawnQueue->next;
        free(head_SpawnQueue);
        head_SpawnQueue = temp_head_SpawnQueue;
    }

    //calling actual process function
    int result = (*cur_func)(arg);

    //terminate on behalf of process after its return
    Terminate(result);
}




/*

SYSTEM CALL HANDLERS

*/
/*
Used to interface with fork from user syscall. Calls fork on the user's behalf.
Queues this function to be run in a SpawnQueue to ensure the correct order. Then
calls fork for user. If fork returns less than 0, then returns -1 to both arg1 and arg4.
Otherwise, sets arg1 to the returned pid, and arg4 to 0.
*/
void Handle_Spawn(USLOSS_Sysargs *args)
{
    //naming args
    char *name = (char*)args->arg5;
    int (*func)(char*) = Trampoline;
    char *arg = (char*)args->arg2;
    int stack_size = (int)(long)args->arg3;
    int priority = (int)(long)args->arg4;

    //create SpawnQueue item
    SpawnQueue *new_SpawnQueue = malloc(sizeof(SpawnQueue));
    new_SpawnQueue->next = NULL;
    new_SpawnQueue->trampoline_func = (int(*)(char*))args->arg1;
    new_SpawnQueue->name = name;

    //enqueue into SpawnQueue
    if (head_SpawnQueue == NULL) {
        head_SpawnQueue = new_SpawnQueue;
        tail_SpawnQueue = new_SpawnQueue;
    }
    else {
        tail_SpawnQueue->next = new_SpawnQueue;
        tail_SpawnQueue = new_SpawnQueue;
    }

    //actual fork call
    int result = fork1(name, func, arg, stack_size, priority);

    //result checking
    if (result < 0) {
        args->arg1 = -1;
        args->arg4 = -1;
    }
    else {
        args->arg1 = result;
        args->arg4 = 0;
    }

    //TODO idk if i should be disabling kernel. doesnt really make sense to do so but its not breaking if i do this
    disable_kernel();
}

/*
Used to interface with join from user syscall. Calls join on the user's behalf. If join returns
a value less than 0, then it is an error and will return -2 to arg4. Otherwise, it returns
the result of join to arg1, the status to arg2 and 0 to arg4.
*/
void Handle_Wait(USLOSS_Sysargs *args)
{
    //internal join call
    int status;
    int result = join(&status);

    //result checking
    if (result < 0) {
        // NOTE spec says undefined. only setting arg4
        args->arg4 = -2; 
    }
    else {
        args->arg1 = result;
        args->arg2 = status;
        args->arg4 = 0;
    }

    disable_kernel();
}

/*
Calls quit on the users behalf. Continiously joins until the process has no
children left. Once join returns -2, then quit can be called with the value
passed into arg1.
*/
void Handle_Terminate(USLOSS_Sysargs *args)
{
    int status;
    while (join(&status) != -2) {}
    disable_kernel();
    quit(args->arg1);
}






/*
MISC FUNCTIONS
*/

/*
Calls currentTime on behalf of user via syscall. Returns result to arg1.
*/
void Handle_GetTimeofDay(USLOSS_Sysargs *args)
{
    args->arg1 = currentTime();
}

/*
Calls readTime on behalf of user via syscall. Returns result to arg1.
*/
void Handle_CPUTime(USLOSS_Sysargs *args)
{
    args->arg1 = readtime();
}

/*
Calls getpid on behalf of user via syscall. Returns result to arg1.
*/
void Handle_GetPID(USLOSS_Sysargs *args)
{
    args->arg1 = getpid();
}









/*
SEMAPHORE FUNCTIONS
*/

/*
If this semaphore isn't already being blocked, then create a new mailbox,
store its id in the blocked_semaphores array for later use in unblocking, then
call Recv on that mailbox to block and await Send.
*/
void SemBlock(int sem_id)
{
    // if theres already a process blocked, recv again, adding it to that mailbox's queue
    if (blocked_semaphores[sem_id] != -1) {
        MboxRecv(blocked_semaphores[sem_id], NULL, 0);
    }
    // otherwise, create a mailbox and block
    else {
        int mbox_id = MboxCreate(1, 0);
        blocked_semaphores[sem_id] = mbox_id;
        MboxRecv(mbox_id, NULL, 0);
    }

}

/*
Calls send on blocked semaphore's mailbox, unblocking it
*/
void SemUnblock(int sem_id)
{
    MboxSend(blocked_semaphores[sem_id], NULL, 0);
    //blocked_semaphores[sem_id] = -1;      // EDIT/TODO: this line breaks test07, removing in the meantime
}


/*
Creates a new semaphore. Finds next avilable semaphore id and stores the value
of this semaphore at that id in the array. returns -1 to arg4 if a semaphore can't be
created. Returns the id of the semaphore to arg1, 0 to arg4 otherwise.
*/
void Handle_SemCreate(USLOSS_Sysargs *args)
{
    int id = find_next_sem();
    if (id < 0) {
        args->arg4 = -1;
    }
    else {
        args->arg1 = id;
        args->arg4 = 0;
        val_semaphores[id] = args->arg1;
    }
}

/*
P operation of a semaphore. If semaphore value is 0, block until it gets incremented, 
then perform the operation once it gets incremented and unblocked.
*/
void Handle_SemP(USLOSS_Sysargs *args)
{
    //semaphore id
    int id = args->arg1;

    //no valid semaphore error
    if (val_semaphores[id] == -1) {
        args->arg4 = -1;
        return;
    }

    //check if value is 0
    if (val_semaphores[id] == 0) {
        //block until nonzero then decrement
        SemBlock(id);
    }

    //decrement semaphore value
    val_semaphores[id] = val_semaphores[id] - 1;
    args->arg4 = 0;
}

void Handle_SemV(USLOSS_Sysargs *args)
{
    //semapohre id
    int id = args->arg1;

    //no valid semaphore error
    if (val_semaphores[id] == -1) {
        args->arg4 = -1;
        return;
    }

    //increment semaphore value then unblock the semaphore if its blocked on P
    val_semaphores[id] = val_semaphores[id] + 1;
    if (blocked_semaphores[id] != -1) {
        //unblock
        SemUnblock(id);
    }
}








/*

NECESSARY EXTERNAL FUNCTIONS

*/
void phase3_init()
{
    //init semaphore arrays
    for (int i=0; i<MAXSEMS; i++) {
        val_semaphores[i] = -1;
    }
    for (int i=0; i<MAXSEMS; i++) {
        blocked_semaphores[i] = -1;
    }

    //init SpawnQueue
    head_SpawnQueue = NULL;
    tail_SpawnQueue = NULL;

    //setting the system call vectors for all handler functions
    systemCallVec[SYS_SPAWN]        = Handle_Spawn;
    systemCallVec[SYS_WAIT]         = Handle_Wait;
    systemCallVec[SYS_TERMINATE]    = Handle_Terminate;
    systemCallVec[SYS_SEMCREATE]    = Handle_SemCreate;
    systemCallVec[SYS_SEMP]         = Handle_SemP;
    systemCallVec[SYS_SEMV]         = Handle_SemV;
    systemCallVec[SYS_GETTIMEOFDAY] = Handle_GetTimeofDay;
    systemCallVec[SYS_GETPROCINFO]  = Handle_GetTimeofDay;
    systemCallVec[SYS_GETPID]       = Handle_GetPID;
}

//lol
void phase3_start_service_processes()
{

}