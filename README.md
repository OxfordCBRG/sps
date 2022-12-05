# Overview

The Slurm Profiling Service (sps) is a lightweight job profiler which bridges the gap between numerical job stats and full-blown application profiling.

# Files

- README.md: this document
- Makefile: "make all" to build sps and the spank plugin
- ccbspank.c: source code for the spank plugin; requires a C compiler and that the Slurm development kit is installed
- ccbspank.sh: called by the spank plugin; allows easy modification without recompiling the plugin
- sps.cpp: source code for sps; requires a C++ compiler with c++-17 support
- taskepilog.sh: needs to be called by the Slurm cleanup scripts; calls sps-stop and then creates the results tarball
- sps-stop: calls cgroup-kill, sps-sum and sps-plot, in that order, to create the output files
- cgroup-kill: kills a running instance of sps in the same cgroup
- sps-sum: creates overview output which sum the data across all processes
- sps-plot: calls gnuplot to create the ascii and png charts
