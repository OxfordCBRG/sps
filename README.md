# Overview

The Slurm (or Simple) Profiling Service `sps` is a lightweight job profiler which bridges the gap between numerical job stats and full-blown application profiling.

`sps` is started by Slurm *as the user* by employing a SPANK plugin which runs at the `slurm_spank_task_init` stage (see https://slurm.schedmd.com/spank.html); at the time of development this is the only possible way to launch a process as the user which is not immediately killed by the scheduler. This plugin calls `sps` and passes it several critical variables which are otherwise unavilable as standard environment variables. `sps` then deamonises and runs until killed. It records CPU, memory & disk reads/writes per binary in an RRD-like memory structure, and writes them out into text logs. After the job finishes, a job eplilogue task (see https://slurm.schedmd.com/prolog_epilog.html) calls a series of linked clean up scripts which visualise the data into both ASCII and PNG plots, then writes the ASCII charts to the Slurm job log and compresses everything into a tarball.

`sps` is Copyright (c) 2022, University of Oxford (see the LICENSE file for details) and was written by Duncan Tooke.

# Requirements

- `sps` currently only has the ability to run automatically with Slurm, though it could be run manually as part of a job on any system (see How To Use below)
- Slurm must be configured so that all jobs run inside their own `cgroup`; this is used by `sps` internally to determine which processes are part of the job
- Compiling the code requires a C compiler, a C++ compiler with c++-17 support and the Slurm development kit
- The output ASCII and PNG charts require that Gnuplot is installed

`sps` has been tested on CentOS 7 and CentOS 9 Stream in x86_64 only.

# Building

- Ensure that you have both `rpm-build` and `rpmdevtools` installed
- If you haven't done so already, run `rpmdev-setuptree` 
- Download the tarball for one of the tagged releases; the file should have a name similar to `sps-3.0.tar.gz`
- Build the source and binary files with `rpmbuild -ta TARFILE`, substituting `TARFILE` for the path to the download
- Install the RPM found in `~/rpmbuild/RPMS/x86_64/` on every node 

Alternatively, untar the source and run `make`, then manually install the necessary files across the cluster.

# Files Installed

- **/usr/share/doc/sps/LICENSE**: the license for the software
- **/usr/share/doc/sps/README.md**: this document
- **/usr/lib64/slurm/launch_sps.so**: the `sps` SPANK plugin
- **/etc/slurm/process_sps.sh**: calls `sps-stop` and then creates the results tarball
- **/usr/bin/ckill**: kills a running instance of a program in the same cgroup
- **/usr/bin/sps**: the sps binary
- **/usr/bin/sps-stop**: calls `ckill`, `sps-sum` and `sps-plot`, in that order, to create the output graphs
- **/usr/bin/sps-sum**: creates overview output which sum the data across all processes
- **/usr/bin/sps-plot**: calls `gnuplot` to create the ASCII and PNG charts

# How To Use

To run automatically for every job, set `optional /usr/lib64/slurm/launch_sps.so` in `/etc/slurm/plugstack.conf` to ensure it is called at job launch, and set `TaskEpilog=/etc/slurm/process_sps.sh` in `slurm.conf` (or add to your existing epilog script) to create the output graphs.

To run interactively, e.g. when testing the code to check that it works, note that you can launch it directly by running `sps` and then kill it as normal using `sps-stop`. The output will be written to a folder called `sps-local`, including all of the typical ASCII and PNG charts. A similar approach could also be used to have on-demand rather than automatic profiling; simply ignore the SPANK plugin and epilogue script, and add the calls directly to the job script:

```
#!/bin/bash
sps

[Job commands here]

sps-stop
``` 

Passing the necessary command line parameters via job environment variables would be a relatively trivial addition, and avoid every job output going into a folder called `sps-local`.

# Notes On The Design

`sps` has been designed to perform with minimal overhead under all conceivable conditions, but also to be relatively easy to understand and maintain. This leads to many deliberate compromises:

- The program has no configurable parameters, such as maximum data points allowed or time between samples; these were tried and found to be confusing, added little value, and led to data size explosions when used badly
- Data is stored by metric (CPU / Memory / Disk Read / Disk Write); this makes the code for writing the four log files easy to understand
- For each metric, data is stored per binary and not per process; per proccess profiling leads to a data size explosion under numerous edge cases and was found to add little additional value
- For each binary, data is stored in a `std::vector` and pushed onto the end every `tick`; a `tick` is not consistently the same time period (see below) and none of the charts make assurances of absolute timescales
- The data vectors are only allowed to grow to a maximum size of 4096 values; this allows a good level of granularity but keeps the data sizes (and thus runtimes and file sizes) manageable
- When the data vectors hit 4096 the program deliberately throws away every other data point, safely stores the others in the first half of the vector, resizes the vector to 2048, and halves the sampling rate; this is a deliberate simplification in the interests of speed and simplicity
- The program starts sampling roughly every second (specifically, it sleeps for 1 second between samples) and slows down as the job run time increases as a result of the sample rate halving each time the data is compacted; this keeps everything small and fast with a reasonable degree of granularity
- CPU usage statistics are based on CPU use versus runtime (like `ps`) and not actual use since the last sampling period (like `top`); this is for speed and simplicity, but has the effect that sudden changes in CPU usage are smoothed out

If you'd like to capture more detail from `sps`, it should be reasonably safe to increase the maximum vector size from 4096 (see the main `try ... catch` at around line 100). If you do, please note that the increase in CPU and memory is roughly linear.

# Contributing to sps

We'd be open to receiving contributions for any of the following:

- Bug fixes or simplicity/reliability/security improvements to the core `sps` or `launch_sps` code
- Improvements to the scripts which produce graphs from the log files
- Packaging the code in non-RPM formats, e.g. DEB
- Integration with schedulers other than SLURM 

For larger changes - especially ones which would result in significantly higher CPU, memory or disk usage - you may be better off forking the project. The focus of `sps` is in being extremely lightweight, and not in developing into a more fully featured profiling tool.

If you do extend `sps`, exercise caution. Early versions demonstrated that both very long running jobs and jobs which spawned unbelieveable numbers of processes were not uncommon, and the CPU/memory implications could be catastrophic.
