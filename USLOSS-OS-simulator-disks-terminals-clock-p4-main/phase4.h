/*
 * These are the definitions for phase 4 of the project (support level, part 2).
 */

#ifndef _PHASE4_H
#define _PHASE4_H

#define MAXLINE         80

extern void phase4_init(void);



/*
 * kernel mode interfaces to the same mechanisms as the syscalls
 */

extern  int  kernSleep(int seconds);

extern  int  kernDiskRead (void *diskBuffer, int unit, int track, int first, 
                           int sectors, int *status);
extern  int  kernDiskWrite(void *diskBuffer, int unit, int track, int first,
                           int sectors, int *status);
extern  int  kernDiskSize (int unit, int *sector, int *track, int *disk);
extern  int  kernTermRead (char *buffer, int bufferSize, int unitID,
                           int *numCharsRead);
extern  int  kernTermWrite(char *buffer, int bufferSize, int unitID,
                           int *numCharsRead);

#endif /* _PHASE4_H */
