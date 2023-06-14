#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <slurm/spank.h>

/* All spank plugins must define this macro for the Slurm plugin loader. */
SPANK_PLUGIN(launch_sps, 1)

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    uint32_t taskid;
    spank_get_item (sp, S_JOB_ID, &taskid);
    char task[64];
    sprintf(task, "%d", taskid);

    uint16_t ncpus;
    spank_get_item (sp, S_JOB_NCPUS, &ncpus);
    char cpus[64];
    sprintf(cpus, "%d", ncpus);

    uint32_t arrnum;
    spank_get_item (sp, S_JOB_ARRAY_ID, &arrnum);
    char arrn[64];
    sprintf(arrn, "%d", arrnum);

    uint32_t arrid;
    spank_get_item (sp, S_JOB_ARRAY_TASK_ID, &arrid);
    char arr[64];
    sprintf(arr, "%d", arrid);

    uint64_t mem;
    spank_get_item (sp, S_JOB_ALLOC_MEM, &mem);
    char memarr[128];
    sprintf(memarr, "%ld", mem);

    char command[512];
    strcpy(command, "/bin/bash -c '/usr/bin/sps ");
    strcat(command,task);
    strcat(command," ");
    strcat(command,cpus);
    strcat(command," ");
    strcat(command,arrn);
    strcat(command," ");
    strcat(command,arr);
    strcat(command," ");
    strcat(command,memarr);
    strcat(command,"'");
    system(command);
    return (0);
}
