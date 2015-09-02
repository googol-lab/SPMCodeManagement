# SPMCodeManagement
- Yooseong Kim, Sep. 2015

This code is an implementation of the worst-case execution time (WCET)-aware code management on scratchpad memory. More details are found in "WCET-Aware Dynamic Code Management on Scratchpads for Software-Managed Multicores", published in the proceedings of the IEEE Real-Time and Embedded Technology and Application Symposium (RTAS),  2014. 

For a given program, this program can:
1) construct an inlined CFG,
2) perform instruction cache analysis by Cullmann 2011*,
3) calculate a WCET estimate using the cache analysis results,
4) calculate a WCET estimate for a given function-to-region mapping in code management on scratchpads,
5) find a function-to-region mapping for minimizing WCET (ILP-based or heuristic),
6) find a region-free mapping for minimizing WCET, and
7) perform a timing simulation for code management on scratchpads using the mapping result from 5) or 6) (by modifing the gem5 simulator+).
---------

What's needed 
- Gurobi solver for solving ILPs (www.gurobi.com)
- ARM cross compiler toolchain (for simulation, use linux toolchains instead of bare metal ones)
- Python 2.7 (3.x is not supported)
---------

How to build and install
  Let $(SPMCM) be the path of the downloaded code. There are two directories under $(SPMCM), CM and sim.
  1. The code management tool (1) - 6) from the above).
    - Edit `toolchain' in $(SPMCM)/CM/ica/py_func.py according to your toolchain name. It is something like "arm-linux-eabi-".
    - Edit `platform' in $(SPMCM)/CM/bin/ica (line 39) accoring to your toolchain name as same as the above. 
    - Edit GUROBIPATH, GUROBIVER, CC, and PYTHONPATH in $(SPMCM)/CM/Makefile according to your system. 
    - Build
      #cd $(SPMCM)/CM
      #make
   
    - Add $(SPMCM)/CM/bin to the PATH envionment variable.
      e.g. #export PATH=$PATH:$(SPMCM)/CM/bin
    - Add $(SPMCM)/CM/ica to PYTHONPATH envionment variable.
      e.g. #export PYTHONPATH:$PYTHONPATH:$(SPMCM)/CM/ica
  
  2. Simulator (7) from the above).
    $(SPMCM)/sim contains modified source files of gem5 (atomic.cc, simulate.cc) and additional files (spm.cc and spm.hh). These can be added to an existing gem5 source tree. In case gem5 gets updated in the future and these files no longer work with the rest of the gem5, I've added a current working version of the entire gem5 source code as gem5.tar.gz in $(SPMCM)/sim. You can simply extract it and build it for ARM as normally you would (ex. #scons build/ARM/gem5.debug)
    - Modify an existing gem5 source code with the files in $(SPMCM)/sim as follows
      Let $(gem5) be the path of the downloaded gem5. 
      #cd $(gem5)
      #cp $(SPMCM)/sim/atomic.cc src/cpu/simple
      #cp $(SPMCM)/sim/simulate.cc src/sim
      #cp $(SPMCM)/sim/spm.* src/sim
    - Modify the scons script so that spm.cc can be compiled, by adding 'Source('spm.cc')' in src/sim/SConscript.
      e.g. 
        ...
        Source('dvfs_handler.cc')
        Source('spm.cc')
      
        if env['TARGET_ISA'] != 'null':
        ...
---------

How to run
  We currently only support programs that can be compiled as simply as `gcc *.c'. All benchmarks in WCET suite and some in the MiBench suite can be compiled like this with little to no modification. Let us say your benchmark is in a directory name 'test'.
  #cd test
  - Generate an inlined CFG and perform cache analysis.
    #cm g <cache_size> <cache_block_size> <associativity>
  - Get an optimal function-to-region mapping by ILP and estimate the WCET, for SPM size X (in bytes).
    #cm test.out X or
  - Get a function-to-region mapping by a heuristic and estimate the WCET, for SPM size X (in bytes).
    #cm test.out X h
  - Get an optimal region-free mapping by ILP and estimate the WCET, for SPM size X (in bytes).
    #cm test.out X orf
---------

* Christoph Cullmann. 2011. Cache persistence analysis: a novel approachtheory and practice. 
 In Proceedings of the 2011 SIGPLAN/SIGBED conference on Languages, compilers and tools for embedded systems (LCTES '11). ACM, New York, NY, USA, 121-130. 
 DOI=10.1145/1967677.1967695 
 http://doi.acm.org/10.1145/1967677.1967695
+ Nathan Binkert et al. 2011. The gem5 simulator. 
 SIGARCH Computer Architecture News 39, 2 (August 2011), 1-7. 
 DOI=10.1145/2024716.2024718
 http://doi.acm.org/10.1145/2024716.2024718 
 http://gem5.org
 
