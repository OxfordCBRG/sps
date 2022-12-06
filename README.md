# Overview

The Slurm Profiling Service `sps` is a lightweight job profiler which bridges the gap between numerical job stats and full-blown application profiling.

`sps` is stared by Slurm *as the user* by employing a SPANK plugin which runs at the `slurm_spank_task_init` stage (see https://slurm.schedmd.com/spank.html); at the time of development this is the only possible way to launch a process as the user which is not immediately killed by the scheduler. This plugin calls a shell script and passes it several critical variables which are otherwise unavilable as standard environment variables. The scripts calls `sps` and passes the variables, then `sps` deamonises and runs until killed. It records CPU, memory & disk reads/writes per binary in an RRD-like memory structure, and writes them out into text logs. After the job finishes, a job eplilogue task (see https://slurm.schedmd.com/prolog_epilog.html) calls a series of linked clean up scripts which visualise the data into both ASCII and PNG plots, then writes the ASCII charts to the Slurm job log and compresses everything into a tarball.

# Requirements

- `sps` only works with Slurm
- Slurm must be configured so that all jobs run inside their own `cgroup`; this is used by `sps` internally to determine which processes are part of the job
- Compiling the code requires a C compiler, a C++ compiler with c++-17 support and the Slurm development kit (a trivially simple GNU Makefile is provided)
- The output ASCII and PNG charts require that Gnuplot is installed on all compute nodes

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

# Notes on data storage and design

`sps` has been designed to perform with minimal overhead under all conceivable conditions, but also to be relatively easy to understand and maintain. This leads to many deliberate compromises:

- The program has no configurable parameters, such as maximum data points allowed or time between samples; these were tried and found to be confusing, added little value, and led to data size explosions when used badly
- Data is stored by metric (CPU / Memory / Disk Read / Disk write); this makes the code for writing the four log files easy to understand
- For each metric, data is stored per binary and not per process; per proccess profiling leads to a data size explosion under numerous edge cases and was found to add little additional value
- For each binary, data is stored in a `std::vector` and pushed onto the end every `tick`; a `tick` is not consistently the same time period (see below) and none of the charts make assurances of absolute timescales
- The data vectors are only allowed to grow to a maximum size of 4096 values; this allows a good level of granularity but keeps the data sizes (and thus runtimes and file sizes) manageable
- When the data vectors hit 4096 the program deliberately throws away every other data point, safely stores the others in the first half of the vector, resizes the vector to 2048, and halves the sampling rate; this is a deliberate simplification in the interests of speed and simplicity
- The program starts sampling roughly every second (specifically, it sleeps for 1 second between samples) and slows down as the job run time increases as a result of the sample rate halving each time the data is compacted; this keeps everything small and fast with a reasonable degree of granularity
- CPU usage statistics are based on CPU use versus runtime (like `ps`) and not actual use since the last sampling period (like `top`); this is for speed and simplicity, but has the effect that sudden changes in CPU usage are smoothed out
