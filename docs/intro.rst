Introduction
============
The Slurm (or Simple) Profiling Service :program:`sps` is a lightweight job
profiler which bridges the gap between numerical job stats and full-blown
application profiling.

The program :doc:`sps` records CPU, memory & disk reads/writes per binary in
the current cgroup in an RRD-like memory structure, and writes them out into
text logs. When build with GPU support it will also monitor any available
NVIDIA or AMD GPUs. The program :doc:`sps-stop` stops the ``sps`` process and
calls :doc:`sps-pyplot` to plot graphs from the time series. See
:doc:`Using SPS <usage>` for detailed description of how to use the profiler.

``sps`` is Copyright (c) 2022, University of Oxford and was written by Duncan
Tooke. Magnus Hagdorn added support for profiling GPU programs, cmake support
and sphinx documentation.

Notes On The Design
-------------------
``sps`` has been designed to perform with minimal overhead under all conceivable
conditions, but also to be relatively easy to understand and maintain. This
leads to many deliberate compromises:

* The program has no configurable parameters, such as maximum data points
  allowed or time between samples; these were tried and found to be
  confusing, added little value, and led to data size explosions when used badly
* Data is stored by metric (CPU / Memory / Disk Read / Disk Write); this makes
  the code for writing the four log files easy to understand
* For each metric, data is stored per binary and not per process; per proccess
  profiling leads to a data size explosion under numerous edge cases and was
  found to add little additional value
* For each binary, data is stored in a ``std::vector`` and pushed onto the end
  every tick; a tick is not consistently the same time period (see below)
  and none of the charts make assurances of absolute timescales
* The data vectors are only allowed to grow to a maximum size of 4096 values;
  this allows a good level of granularity but keeps the data sizes
  (and thus runtimes and file sizes) manageable
* When the data vectors hit 4096 the program deliberately throws away every
  other data point, safely stores the others in the first half of the vector,
  resizes the vector to 2048, and halves the sampling rate; this is a deliberate
  simplification in the interests of speed and simplicity
* The program starts sampling roughly every second (specifically, it sleeps for
  1 second between samples) and slows down as the job run time increases as a
  result of the sample rate halving each time the data is compacted; this
  keeps everything small and fast with a reasonable degree of granularity
* CPU usage statistics are based on CPU use versus runtime (like ps) and not actual
  use since the last sampling period (like top); this is for speed and simplicity,
  but has the effect that sudden changes in CPU usage are smoothed out
