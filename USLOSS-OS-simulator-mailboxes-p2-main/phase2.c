#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phase2.h"
#include <phase1.h>
#include <usloss.h>
#include <usyscall.h>
#include <libdisk.h>
#include <libuser.h>

/*
Represents a mailslot. Contains the message itself and the message size.
*/
typedef struct Mailslot 
{
    //void *message;
    char message[MAX_MESSAGE];
    int message_size;
    struct Mailslot *next_slot;

} Mailslot;

/*
Represents the queue for processes. Contains the pid for unblocking.
*/
typedef struct ProcessQueue
{
    int pid;
    struct ProcessQueue *next;

} ProcessQueue;

/*
Core mailbox structure. Contains an id, state, and metadata about the mailbox.
*/
typedef struct Mailbox 
{

    int id;

    int state;

    int num_slots;
    int slot_size;
    int slots_consumed;

    Mailslot *slot_queue_head;
    Mailslot *slot_queue_tail;

    ProcessQueue *send_queue_head;
    ProcessQueue *send_queue_tail;

    ProcessQueue *recv_queue_head;
    ProcessQueue *recv_queue_tail;

} Mailbox;





void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

Mailbox *mailboxes[MAXMBOX]; //MAXMBOX
Mailslot *mailslots[MAXSLOTS]; //MAXSLOTS
int mailbox_count;      //how many mailboxes currently exist
int mailslot_count;     //how many mailslots are currently being used

int waitDevice_blocked;     //0 if a device is not blocked in waitDevice 1 if it is






/*
Helper Functions
*/
int check_kernel() 
{
    unsigned int psr = USLOSS_PsrGet();
    return (psr & 0x1);        // should return 0 if kernel is disabled, 1 if enabled
}

void disable_interrupts() 
{
    unsigned int psr = USLOSS_PsrGet();
    // if the current psr has interrupts enabled, disable them
    if (psr >= 3) {
        psr = 1;
        USLOSS_PsrSet(psr);
    }
}

int psr_helper() {
    unsigned int psr = USLOSS_PsrGet();
    if (check_kernel() == 0) { return 0; }
    disable_interrupts();
    return 1;
}


/*
Debug tool. Dumps all mailboxes, related mailslots and block queues.
*/
void DumpMailboxes()
{
    USLOSS_Console("==================================DUMPING MAILBOXES=======================================\n");
    for (int i=7; i<MAXMBOX; i++) {
        Mailbox *cur_mbox = mailboxes[i];
        if (cur_mbox != NULL) {
            USLOSS_Console("          ==============================MAILBOX %d==============================\n", i);
            USLOSS_Console("| state: %d | num_slots: %d | slots_consumed: %d | slot_size: %d |\n\n", cur_mbox->state, cur_mbox->num_slots, cur_mbox->slots_consumed, cur_mbox->slot_size);
            
            if (cur_mbox->slot_queue_head == NULL) { USLOSS_Console("Slot Head: NULL\n"); }
            else { USLOSS_Console("Slot Head: %s\n", cur_mbox->slot_queue_head->message); }
            Mailslot *cur_slot = cur_mbox->slot_queue_head;
            while (cur_slot != NULL) {
                USLOSS_Console(" | %s | ", cur_slot->message);
                cur_slot = cur_slot->next_slot;
            }
            if (cur_mbox->slot_queue_tail == NULL) { USLOSS_Console("Slot Tail: NULL\n"); }
            else { USLOSS_Console("\nSlot Tail: %s\n\n", cur_mbox->slot_queue_tail->message); }


            if (cur_mbox->send_queue_head == NULL) { USLOSS_Console("Send Head: NULL\n"); }
            else { USLOSS_Console("Send Head: %d\n", cur_mbox->send_queue_head->pid); }
            ProcessQueue *cur_send = cur_mbox->send_queue_head;
            while (cur_send != NULL) {
                USLOSS_Console(" | %d | ", cur_send->pid);
                cur_send = cur_send->next;
            }
            if (cur_mbox->send_queue_tail == NULL) { USLOSS_Console("Send Tail: NULL\n"); }
            else { USLOSS_Console("\nSend Tail: %d\n\n", cur_mbox->send_queue_tail->pid); }
            
            if (cur_mbox->recv_queue_head == NULL) { USLOSS_Console("Recv Head: NULL\n"); }
            else { USLOSS_Console("Recv Head: %d\n", cur_mbox->recv_queue_head->pid); }
            ProcessQueue *cur_recv = cur_mbox->recv_queue_head;
            while (cur_recv != NULL) {
                USLOSS_Console(" | %d | ", cur_recv->pid);
                cur_recv = cur_recv->next;
            }
            if (cur_mbox->recv_queue_tail == NULL) { USLOSS_Console("Recv Tail: NULL\n"); }
            else { USLOSS_Console("\nRecv Tail: %d\n\n", cur_mbox->recv_queue_tail->pid); }
            
            USLOSS_Console("          ===================================================================\n");
        }
    }
    USLOSS_Console("==================================ENDING THE DUMP=========================================\n");
}







/*

=========== NECESSARY FUNCTIONS =========

*/
void nullsys(USLOSS_Sysargs *args)
{
    USLOSS_Console("nullsys(): Program called an unimplemented syscall.  syscall no: %d   PSR: 0x0%x\n", args->number, USLOSS_PsrGet());
    USLOSS_Halt(1);
}

/*
Function called by the interrupt vector for syscalls. Currently does a simple check for the
passed argument then calls the appropriate syscall function. Right now its just nullsys.
*/
void syscallHandler(int arg1, void *arg2)
{
    USLOSS_Sysargs *arg = arg2;
    if (arg->number >= 50) {
        USLOSS_Console("syscallHandler(): Invalid syscall number %d\n", arg->number);
        USLOSS_Halt(1);
    }
    systemCallVec[arg->number](arg);
}


/*

=========== END NECESSARY FUNCTIONS =========

*/







/*

=========== REQUIRED FUNCTIONS =========

*/

/*
Initializes important information. Sets all global arrays to NULL, or nullsys for syscallvec
Creates 7 device mailboxes, and sets the syscallHandler function in the IntVec array
*/
void phase2_init(void) 
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    //set all values of syscallvec array to nullsys
    for (int i=0; i<MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
    }
    //set all values of mailboxes array to nullsys
    for (int i=0; i<MAXMBOX; i++) {
        mailboxes[i] = NULL;
    }
    //set all values of mailslots array to nullsys
    for (int i=0; i<MAXSLOTS; i++) {
        mailslots[i] = NULL;
    }

    // 7 device mailboxes
    mailbox_count = 0;
    for (int i=0; i<=6; i++) { MboxCreate(10, 10); }
    mailslot_count = 0;

    //interrupt init
    //USLOSS_IntVec[USLOSS_CLOCK_INT] = phase2_clockHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;

    waitDevice_blocked = 0;

    USLOSS_PsrSet(psr);
}



/*
MboxCreate() Helper Functions/stuff
*/
//TODO not necessary anymore??
//int current_mailbox = 7;
/*
Returns the next available mailbox id
*/
int FindNextMboxID()
{
    for (int i=0; i<MAXMBOX; i++) {
        if (mailboxes[i] == NULL) {
            //current_mailbox = i;
            return i;
        }
    }

    return -1;
}

/*
Initializes the mailbox and saves it to the global array.
*/
int MboxSave(int slots, int slot_size)
{
    Mailbox *new_mailbox = malloc(sizeof(Mailbox));

    int id = FindNextMboxID();
    new_mailbox->id = id;

    new_mailbox->state = 1;

    new_mailbox->num_slots = slots;
    new_mailbox->slot_size = slot_size;
    new_mailbox->slots_consumed = 0;

    new_mailbox->slot_queue_head = NULL;
    new_mailbox->slot_queue_tail = NULL;

    new_mailbox->send_queue_head = NULL;
    new_mailbox->send_queue_tail = NULL;

    new_mailbox->recv_queue_head = NULL;
    new_mailbox->recv_queue_tail = NULL;

    mailboxes[id] = new_mailbox;
    mailbox_count++;

    return id;
}

/*
Creates a new mailbox and puts it in the global array. Returns -1 if invalid 
arguments were passed, otherwise returns the id of the newly created mailbox.
*/
int MboxCreate(int slots, int slot_size)
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    //error condition checking
    //-1 if slots or slot_size is negative, or larger than allowed 
    if (slots < 0 || slot_size < 0 || slot_size > MAX_MESSAGE || slots > MAXSLOTS) { return -1; }
    if (mailbox_count >= MAXMBOX) { return -1; }

    USLOSS_PsrSet(psr);

    return MboxSave(slots, slot_size);

}





/*
MboxRelease Helper Functions
*/

/*
Handles freeing all pointers and queues within a mailbox.
*/
void FreeQueues(int id) 
{
    Mailbox *current_mailbox = mailboxes[id];

    //release all mailslots in this mailbox
    Mailslot *cur_slot = current_mailbox->slot_queue_head;
    while (cur_slot != NULL && cur_slot != current_mailbox->slot_queue_tail) {
        Mailslot *free_slot = cur_slot;
        if (cur_slot->next_slot != NULL) {
            cur_slot = cur_slot->next_slot;
        }
        free(free_slot);
        mailslot_count--;       //decrement slot counter
    }
    //release tail
    if (current_mailbox->slot_queue_tail != NULL) {
        free(current_mailbox->slot_queue_tail);
        mailslot_count--;       //decrement slot counter
    }

    //release send queue in this mailbox
    ProcessQueue *cur_proc = current_mailbox->send_queue_head;
    while (cur_proc != NULL && cur_proc != current_mailbox->send_queue_tail) {
        ProcessQueue *free_proc = cur_proc;
        if (cur_proc->next != NULL) {
            cur_proc = cur_proc->next;
        }
        int val = unblockProc(free_proc->pid);
        free(free_proc);
    }
    //release tail
    if (current_mailbox->send_queue_tail != NULL) {
        int val = unblockProc(current_mailbox->send_queue_tail->pid);
        free(current_mailbox->send_queue_tail);
    }

    //release recv queue in this mailbox
    cur_proc = current_mailbox->recv_queue_head;
    while (cur_proc != NULL && cur_proc != current_mailbox->recv_queue_tail) {
        ProcessQueue *free_proc = cur_proc;
        if (cur_proc->next != NULL) {
            cur_proc = cur_proc->next;
        }
        int val = unblockProc(free_proc->pid);
        free(free_proc);
    }
    //release tail
    if (current_mailbox->recv_queue_tail != NULL) {
        int val = unblockProc(current_mailbox->recv_queue_tail->pid);
        free(current_mailbox->recv_queue_tail);       
    }

}

/*
Frees the mailbox, and sets that id to NULL
*/
void FreeMailbox(int id)
{
    FreeQueues(id);
    free(mailboxes[id]);
    mailbox_count--;
    mailboxes[id] = NULL;
}


/*
Function called for releasing an existing mailbox. Returns 0 if successful,
and -1 if invalid arguments are passed.
*/
int MboxRelease(int mbox_id) 
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    //error condition checking
    //return -1 if ID of mailbox is not currently in use
    if (mailboxes[mbox_id] == NULL) { return -1; }

    mailboxes[mbox_id]->state = 0;
    FreeMailbox(mbox_id);

    USLOSS_PsrSet(psr);

    return 0;
}










/*
General private helpers for Recv and Send
*/

/*
Shared internal send function. Also includes a mode paramater, where 0 means the process
will block if necessary, 1 means the function will return -2 if blocking is necessary.

Function returns -3 if the mailbox was released, -2 if there are no global mailbox slots, 
-1 if the arguments are invalid or mailbox doesn't exist, and 0 if everything was successful.

If all of the current mailbox's slots are consumed, the process blocks and puts itself into a
process queue, waiting for Recv to unblock it eventually. Next, it will copy the contents of its message
to a new mailslot to the tail of the slot queue. It then wakes up the head of the recv queue, if necessary,
and returns 0.
*/
int Send(int mbox_id, void *msg_ptr, int msg_size, int mode)
{
    //0 mode means block, 1 mode means return

    //error condition checking
    //-3 if the mailbox was released
    //-2 system has run out of global mailbox slots, message cant be queued
    //-1 invalid args
    // 0 otherwise
    if (msg_ptr == NULL && msg_size > 0) { return -1; }             // null pointer to non zero size
    if (mailboxes[mbox_id] == NULL) { return -1; }                  // mailbox does not exist
    if (mailboxes[mbox_id]->state != 1) { return -3; }              // mailbox is released
    if (msg_size >= MAX_MESSAGE) { return -1; }                     // msg too long
    if (msg_size > mailboxes[mbox_id]->slot_size) { return -1; }    // msg wont fit in mailslot
    if (mailslot_count == MAXSLOTS) { return -2; }                  // not enough global slots



    Mailbox *cur_mailbox = mailboxes[mbox_id];

    //CHECK BLOCK CONDITION
    // put it into the next available mail slot, if it isn't the first
    if (cur_mailbox->num_slots == cur_mailbox->slots_consumed) {        //check if all slots are consumed, need to block
        if (mode == 0) {

            // queue this process up for sending
            ProcessQueue *new_process = malloc(sizeof(ProcessQueue));
            new_process->pid = getpid();
            new_process->next = NULL;
            if (cur_mailbox->send_queue_head == NULL) {     //if queue is empty, set this as head and tail
                cur_mailbox->send_queue_head = new_process;
                cur_mailbox->send_queue_tail = new_process;
            }
            else {          //if queue is not empty, make this the tail
                cur_mailbox->send_queue_tail->next = new_process;
                cur_mailbox->send_queue_tail = new_process;
            }

            // block
            blockMe(11);
            //TODO idk how its possible... i ONLY set the state to 0 or 1 but its being set as 5 for some reason??????
            if (cur_mailbox->state != 1) { return -3; }     //mailbox was destroyed, need to return after unblock

        }
        else {
            return -2;
        } 
    }

    //NEW MAILSLOT
    //if not all slots are consumed, try to put in next avialable mail slot
    //initialize new mailslot
    Mailslot *new_mailslot = malloc(sizeof(Mailslot));
    cur_mailbox->slots_consumed = cur_mailbox->slots_consumed + 1;          //increment the number of slots consumed
    mailslot_count++;

    memcpy(new_mailslot->message, msg_ptr, msg_size);
    new_mailslot->message_size  = msg_size;
    new_mailslot->next_slot     = NULL;

    if (cur_mailbox->slot_queue_tail == NULL && cur_mailbox->slot_queue_head == NULL) {     //if the queue is empty
        //make this new slot both the head and the tail
        cur_mailbox->slot_queue_head = new_mailslot;
        cur_mailbox->slot_queue_tail = new_mailslot;
    }
    else {      // if queue has elements
        //make next slot after current queue tail this new slot, and make IT the new tail
        cur_mailbox->slot_queue_tail->next_slot = new_mailslot;
        cur_mailbox->slot_queue_tail = new_mailslot;
    }

    // DEQUEUE RECV
    // when sending, if there is a reciver in queue, unblock it and deliver that message, free that queue slot
    if (cur_mailbox->recv_queue_head != NULL) {
        ProcessQueue *cur_recv_queue_head = cur_mailbox->recv_queue_head;
        int pid = cur_recv_queue_head->pid;
        if (cur_recv_queue_head->next == NULL) {        //last element in queue, making head and tail null
            cur_mailbox->recv_queue_head = NULL;
            cur_mailbox->recv_queue_tail = NULL;
        }
        else {          // make this element's next node the new head
            cur_mailbox->recv_queue_head = cur_recv_queue_head->next;
        }
        free(cur_recv_queue_head);
        int val = unblockProc(pid);
    }

    return 0;

}

/*
Operates in reverse logic to send.
Shared internal recv function. Also includes a mode paramater, where 0 means the process
will block if necessary, 1 means the function will return -2 if blocking is necessary.

Function returns -3 if the mailbox was released, -1 if the arguments are invalid or 
mailbox doesn't exist, and 0 if everything was successful.

If the slot queue is currently empty, this process will be placed at the tail of the recv
process queue and block. Next, it recieves the current head of the send queue, copies its
contents, and wakes up the head of the send queue if necessary. 
*/
int Recv(int mbox_id, void *msg_ptr, int msg_max_size, int mode)
{
    //error condition checking
    //-3 if the mailbox was released
    //-1 invalid args
    if (msg_ptr == NULL && msg_max_size > 0) { return -1; }         // null pointer to non zero size
    if (mailboxes[mbox_id] == NULL) { return -1; }                  // mailbox does not exist
    if (mailboxes[mbox_id]->state != 1) { return -3; }              // mailbox was released
    if (msg_max_size > MAX_MESSAGE) { return -1; }                  // msg too long


    Mailbox *cur_mailbox = mailboxes[mbox_id];
    if (cur_mailbox->slot_queue_head == NULL) {         // if there are no messages to recive block and wait

        if (mode == 0) {
            // block
            // queue this process up for sending
            ProcessQueue *new_process = malloc(sizeof(ProcessQueue));
            new_process->pid = getpid();
            new_process->next = NULL;
            if (cur_mailbox->recv_queue_head == NULL) {     //if queue is empty, set this as head and tail
                cur_mailbox->recv_queue_head = new_process;
                cur_mailbox->recv_queue_tail = new_process;
            }
            else {          //if queue is not empty, make this the tail
                cur_mailbox->recv_queue_tail->next = new_process;
                cur_mailbox->recv_queue_tail = new_process;
            }

            // block
            blockMe(11);
            if (cur_mailbox->state != 1) { return -3; }     //mailbox was destroyed, need to return after unblock

        }

        else {
            return -2;
        }
    }

    // there is a message to recieve
    // dequeue message, set next message as head, wake up next sender if there is one, free this mailbox
    Mailslot *cur_mailslot = cur_mailbox->slot_queue_head;
    void *recv_message = cur_mailslot->message;
    int recv_message_size = cur_mailslot->message_size;
    if (msg_max_size < recv_message_size) { return -1; }        //if the buffer is less than the length of the messsage, error.
    memcpy(msg_ptr, recv_message, recv_message_size);

    //setting next head for slot queue
    if (cur_mailslot->next_slot == NULL) {
        cur_mailbox->slot_queue_head = NULL;
        cur_mailbox->slot_queue_tail = NULL;
    }
    else {
        cur_mailbox->slot_queue_head = cur_mailslot->next_slot;
    }
    
    free(cur_mailslot);
    mailslot_count--;       //decrement global slot count
    cur_mailbox->slots_consumed = cur_mailbox->slots_consumed - 1;      //decrement internal slot count

    //wake next queued sender, if there are any
    //same as in send section, if something changes, change there too
    if (cur_mailbox->send_queue_head != NULL) {
        ProcessQueue *cur_send_queue_head = cur_mailbox->send_queue_head;
        int pid = cur_send_queue_head->pid;
        if (cur_send_queue_head->next == NULL) {        //last element in queue, making head and tail null
            cur_mailbox->send_queue_head = NULL;
            cur_mailbox->send_queue_tail = NULL;
        }
        else {          // make this element's next node the new head
            cur_mailbox->send_queue_head = cur_send_queue_head->next;
        }
        free(cur_send_queue_head);
        int val = unblockProc(pid);
    }

    //recieve message
    return recv_message_size;
}




/*
Send and Recv caller block. These functions simply call Send or Recv with the
correct mode. Called externally by testcases, they return whatever Send or Recv returned.
*/
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    return Send(mbox_id, msg_ptr, msg_size, 0);
    USLOSS_PsrSet(psr);
}

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size)
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    return Send(mbox_id, msg_ptr, msg_size, 1);
    USLOSS_PsrSet(psr);
}



int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size)
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    return Recv(mbox_id, msg_ptr, msg_max_size, 0);
    USLOSS_PsrSet(psr);
}


int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size)
{
    unsigned int psr = USLOSS_PsrGet();
    disable_interrupts();

    return Recv(mbox_id, msg_ptr, msg_max_size, 1);
    USLOSS_PsrSet(psr);
}












// type = interrupt device type, unit = # of device (when more than one),
// status = where interrupt handler puts device's status register.
/*
Called to recieve payloads from devices on certian mailboxes. Will block
while waiting for their arrival and unblock when recieved. 
*/
void waitDevice(int type, int unit, int *status)
{
    if (unit < 0 || unit > 3) {
        USLOSS_Console("Error: invalid unit given to waitDevice.\n");
        USLOSS_Halt(1);
    }

    //recieve payload on mbox 0, the clock
    if (type == USLOSS_CLOCK_DEV && mailboxes[0]->slot_queue_head != NULL) {
        int *buffer;
        waitDevice_blocked = 1;
        MboxRecv(0, buffer, sizeof(buffer));
        waitDevice_blocked = 0;
        *status = *buffer;
    }



}


void wakeupByDevice(int type, int unit, int status)
{
    
}



/*
Currently a no op since nothing useful is needed here.
*/
void phase2_start_service_processes(void)
{

}

/*
Returns if waitDevice is blocked. 0 if it isn't, 1 if it is.
*/
int phase2_check_io(void)
{
    return waitDevice_blocked;
}




// phase 1 NEVER calls phase2_clockHandler in test13. no idea why...its sometimes called in other testcases though, so obviously it works.
// i can't really test the send and recv functionality so this probably doesn't work at all
/*
Called by phase1 (allegedly) when a clock interrupt fires. Reports the current time
in microseconds for every 100 milliseconds that pass. It sends this payload to mailbox
0 where it is then recieved by waitDevice.
*/
int prev_time = 0;
void phase2_clockHandler(void)
{
    int cur_time;
    cur_time = currentTime();
    //has 100 ms passed? (deviceinput returns microseconds)
    int time_passed = cur_time - prev_time;
    if (time_passed >= 100000) {
        MboxSend(0, &cur_time, sizeof(cur_time));
        prev_time = cur_time;
    }
}

/*

=========== END OF REQUIRED FUNCTIONS =========

*/