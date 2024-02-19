Installation
============

Requirements
------------
* Slurm must be configured so that all jobs run inside their own ``cgroup``.
  This is used by sps internally to determine which processes are part of the
  job.
* Compiling the code requires, cmake, a C compiler, a C++ compiler with c++-17
  and libboost support.
* If you want to profile GPU code you need to have CUDA with the `NVIDIA
  Management Library
  <https://developer.nvidia.com/nvidia-management-library-nvml>`__ and/or the
  `AMD ROCm SMI library
  <https://rocm.docs.amd.com/projects/rocm_smi_lib/en/latest/>`__
  installed.
* If you want to use the SPANK plugin you need to have the SLURM development
  libraries installed.
* Plotting the results requres python3 with `pandas
  <https://pandas.pydata.org/>`__ and `matplotlib <https://matplotlib.org/>`__.
* The documentation is generated using `sphinx
  <https://www.sphinx-doc.org/en/master/>`__.

Building
--------
The build system uses `cmake <https://cmake.org/>`__::
  
  mkdir build
  cd build
  cmake ..
  make
  make install


rpm Package
-----------
RPM packages can be built using `git buidpackage
<https://honk.sigxcpu.org/projects/git-buildpackage/manual-html/>`__.

* Ensure that you have ``rpm-build``, ``rpmdevtools`` and ``gbp-rpm`` installed
* Check out the ``rpm`` branch
* create the srpm: ``gbp buildpackage-rpm -bs``
* build rpm using: ``mock rebuild sps-4.0-1.src.rpm``

deb Package
-----------
deb packages are also built using `git buidpackage
<https://honk.sigxcpu.org/projects/git-buildpackage/manual-html/>`__.

* Check out the ``debian`` branch
* build paclage: ``gbp buildpackage``
