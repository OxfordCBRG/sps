# Overview

The Slurm Profiling Service `sps` is a lightweight job profiler which bridges the gap between numerical job stats and full-blown application profiling.

`sps` is stared by Slurm *as the user* by employing a SPANK plugin which runs at the `slurm_spank_task_init` stage (see https://slurm.schedmd.com/spank.html); at the time of development this is the only possible way to launch a process as the user which is not immediately killed by the scheduler. This plugin calls a shell script and passes it several critical variables which are otherwise unavilable as standard environment variables. The scripts calls `sps` and passes the variables, then `sps` deamonises and runs until killed. It records CPU, memory & disk reads/writes per binary in an RRD-like memory structure, and writes them out into text logs. After the job finishes, a job eplilogue task (see https://slurm.schedmd.com/prolog_epilog.html) calls a series of linked clean up scripts which visualise the data into both ASCII and PNG plots, then writes the ASCII charts to the Slurm job log and compresses everything into a tarball.

# Requirements

`sps` only works with Slurm, and Slurm must be set up so that all jobs run inside their own `cgroup`. The `cgroup` is used by `sps` to determine which processes are part of the job.

Compiling the code requires a C compiler, a C++ compiler with c++-17 support and the Slurm development kit. A trivially simple GNU Makefile is provided, if desired.

# Files

- *README.md*: this document
- *Makefile*: `make all` to build `sps` and the spank plugin
- *ccbspank.c*: source code for the spank plugin; set `optional /usr/lib64/slurm/ccbspank.so` in `plugstack.conf` to ensure it is called at job launch
- *ccbspank.sh*: called by the spank plugin; allows easy modification without recompiling the plugin
- *sps.cpp*: source code for `sps`
- *taskepilog.sh*: needs to be set to run under `TaskEpilog` in `slurm.conf`; calls `sps-stop` and then creates the results tarball
- *sps-stop*: calls `cgroup-kill`, `sps-sum` and `sps-plot`, in that order, to create the output files
- *cgroup-kill*: kills a running instance of `sps` in the same cgroup
- *sps-sum*: creates overview output which sum the data across all processes
- *sps-plot*: calls `gnuplot` to create the ascii and png charts

On the CCB systems, the files are installed as follows:

```
/package/sps/current/
                     sps
                     cgroup-kill
                     sps-stop
                     sps-sum
                     sps-plot
/etc/slurm/
                     ccbspank.sh
                     taskepilog.sh
/usr/lib64/slurm
                     ccpspank.so
```

The files must be available on all compute nodes and the Slurm master. Several of them contain hard coded paths that will need to be updated for local use (this could be improved in future versions).

# Running locally

When testing the code to check that it works, note that you can run it directly in any login session and then kill it as normal using `sps-stop`. The output will be written to a folder called `sps-local`.
