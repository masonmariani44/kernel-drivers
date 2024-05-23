#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdio.h>
#include <stdlib.h>

void Handle_DiskSize(USLOSS_Sysargs *args)
{
    int unit = args->arg1;
    
    //error checking
    if (unit != 0 || unit != 1) {
        args->arg4 = -1;
        return;
    }

    //512 byte block 16 blocks each track is 8kb
    //query how many tracks on the disk
    int num_tracks;
    USLOSS_DeviceRequest *request = malloc(sizeof(USLOSS_DeviceRequest));
    request->opr  = USLOSS_DISK_TRACKS;
    request->reg1 = &num_tracks; 
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, request);

    //output
    args->arg1 = 512;
    args->arg2 = 16;
    args->arg3 = num_tracks;



    args->arg4 = 0;

}

void phase4_init()
{
    systemCallVec[SYS_DISKSIZE] = Handle_DiskSize;
}

void phase4_start_service_processes()
{
    return;
}
