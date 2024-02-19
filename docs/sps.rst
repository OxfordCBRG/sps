sps
===

.. program:: sps

Synopsis
--------

**sps** [ *options* ]

Description
-----------

:program:`sps` starts the Slurm (or Simple) Profiling Service. It monitors all
processes running in the same cgroup. Time series of cpu time, memory usage,
read and write operations are stored in a text files. GPU load, memory and power
usage are also recorded if GPUs are present.

:program:`sps` can take information from environment variables set by the SLURM
scheduler (see `Environment Variables`_). Time series are written to the *SPSDIR*.
The *SPSDIR* is either derived from the SLURM job ID or the SLURM array job ID and
array task ID. If no job/array job ID is found, it uses the directory `sps-local`.
The *SPSDIR* is rotated up to 9 times if it already exists.

The program keeps a fixed number of records in ASCII time series files. These can be
plotted using :program:`sps-pyplot`.

Options
-------

.. option:: -h, --help

  Print help message.

.. option:: -f, --foreground

  Run sps in foreground, otherwise daemonize it.

.. option:: -j, --job ID

  The SLURM job ID.

.. option:: -c, --ncpus num

  The number of CPUs used by this job.

.. option:: -a, --array-id ID

  Specify the array ID if the the job is an array job.

.. option:: -t, --array-task task

  The array task number.

.. option:: -p, --prefix prefix

  Create output directory in `prefix`. If not set, write to present working
  directory.

Environment Variables
---------------------

:program:`sps` uses environment variables set by the SLURM scheduler if not
overridden by command line options.

The following SLURM environment variables are used if found:

* SLURM_JOB_ID: the SLURM job ID, used for the name of the *SPSDIR*
* SLURM_CPUS_ON_NODE: the number of CPUs on this node, used by this job
* SLURM_ARRAY_JOB_ID: the array job ID, used instead of the SLURM job ID if the
  job is an array job.
* SLURM_ARRAY_TASK_ID: the array task ID, is used together with the array job
  ID.

In addation to environment variables set by SLURM, the following variable is
also used:

* SPS_PREFIX: Prefix where *SPSDIR* is found. Use current working directory
  when the environment variable is not set. The *-p* option takes presedence
  over the environment variable.

See also
--------

:doc:`sps-pyplot` (1)
