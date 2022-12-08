#!/bin/bash

# For regular jobs, the Job ID is sufficient. However, for array jobs,
# we need to reconstruct the special array job ID and set the Job ID
# to that.
JID=${SLURM_JOB_ID}
AID=${SLURM_ARRAY_JOB_ID}
TID=${SLURM_ARRAY_TASK_ID}
if [ ! -z $AID ] && [ ! -z $TID ]; then
  JID=`echo ${AID}_${TID}`
fi

/usr/bin/sps-stop

# Now, get out output file from "scontrol" and check the directory that
# it's pointing at
OUT=`/usr/bin/scontrol show jobid ${JID} | /usr/bin/sed -n '/StdOut/s/^.*=//p'`
DIR=`/usr/bin/dirname "$OUT"`

# So long as that's a real directory, we should be good
if [ ! -z "$DIR" ] && [ -d "$DIR" ]; then
  cat sps-${JID}/sps-${JID}.log \
      sps-${JID}/sps-${JID}-cpu.tsv.sum.ascii \
      sps-${JID}/sps-${JID}-mem.tsv.sum.ascii \
      sps-${JID}/sps-${JID}-read.tsv.sum.ascii \
      sps-${JID}/sps-${JID}-write.tsv.sum.ascii 1>> $OUT 2>/dev/null
      tar --remove-files -czf sps-${JID}.tar.gz sps-${JID} 2>/dev/null
fi

