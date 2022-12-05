#!/bin/bash

# Gets called for every job and every array task. Arguments are:
#
# JOBID REQUESTED_CPUS ARRAY_ID ARRAY_TASK
#
# Normal jobs have ARRAY_ID = 0, ARRAY_TASK = -2 so it's always
# valid to use both

/package/sps/current/sps $1 $2 $3 $4
