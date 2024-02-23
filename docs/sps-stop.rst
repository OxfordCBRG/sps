sps-stop
========

.. program:: sps-stop

Synopsis
--------

**sps-stop** [ *options* ]

Description
-----------

:program:`sps-stop` uses :program:`ckill` to stop :program:`sps`. Once stopped
it will use :program:`sps-pyplot` to plot the resulting time series.

Options
-------

.. option:: -h

  Print help message.

.. option:: -p PREFIX

  Look for *SPSDIR* in PREFIX rather than current working directory.

.. option:: -o OUTPUT

   Write plot to OUTPUT. The format is determined by the file extension.
   By default a png image will be written to the *SPSDIR*

Environment Variables
---------------------

* SPS_PREFIX: Prefix where *SPSDIR* is found. Use current working directory
  when the environment variable is not set. The *-p* option takes presedence
  over the environment variable.

See also
--------

:doc:`sps` (1),
:doc:`ckill` (1),
:doc:`sps-pyplot` (1)
