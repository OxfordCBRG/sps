#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <slurm/spank.h>

/* All spank plugins must define this macro for the Slurm plugin loader. */
SPANK_PLUGIN(launch_sps, 1);

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    int taskid;
    spank_get_item (sp, S_JOB_ID, &taskid);
    char task[64];
    sprintf(task, "%d", taskid);

    int ncpus;
    spank_get_item (sp, S_JOB_NCPUS, &ncpus);
    char cpus[64];
    sprintf(cpus, "%d", ncpus);

    int arrnum;
    spank_get_item (sp, S_JOB_ARRAY_ID, &arrnum);
    char arrn[64];
    sprintf(arrn, "%d", arrnum);

    int arrid;
    spank_get_item (sp, S_JOB_ARRAY_TASK_ID, &arrid);
    char arr[64];
    sprintf(arr, "%d", arrid);

    char command[512];
    strcpy(command, "/bin/bash -c '/usr/bin/sps -j ");
    strcat(command,task);
    strcat(command," -c ");
    strcat(command,cpus);
    if (arrnum > 0) {
      strcat(command," -a ");
      strcat(command,arrn);
      strcat(command," -t ");
      strcat(command,arr);
    }
    strcat(command,"'");
    system(command);
    return (0);
}
