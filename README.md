# Overview

The Slurm Profiling Service (sps) is a lightweight job profiler which bridges the gap between numerical job stats and full-blown application profiling.

# Files

- *README.md*: this document
- *Makefile*: `make all` to build `sps` and the spank plugin
- *ccbspank.c*: source code for the spank plugin; requires a C compiler and that the Slurm development kit is installed
- *ccbspank.sh*: called by the spank plugin; allows easy modification without recompiling the plugin
- *sps.cpp*: source code for `sps`; requires a C++ compiler with c++-17 support
- *taskepilog.sh*: needs to be called by the Slurm cleanup scripts; calls `sps-stop` and then creates the results tarball
- *sps-stop*: calls `cgroup-kill`, `sps-sum` and `sps-plot`, in that order, to create the output files
- *cgroup-kill*: kills a running instance of `sps` in the same cgroup
- *sps-sum*: creates overview output which sum the data across all processes
- *sps-plot*: calls `gnuplot` to create the ascii and png charts

On the CCB systems, the files are installed as follows:

```
/project/sps/current/
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

Several of these are hard coded paths that will need to be updated for local use. This could be improved in future versions.

# Running locally

When testing the code to check that it works, note that you can run it directly in any login session and then kill it as normal using `sps-stop`. The output will be written to a folder called `sps-local`.
