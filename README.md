# HaliVer Experiments
These are the experiments conducted with HaliVer. This artifact is prepared together with the [TACAS 23 Artifact Evaluation VM - Ubuntu 22.04 LTS](https://zenodo.org/records/7113223).


### HaliVer
The HaliVer tool itself is a fork of the Halide compiler and can be found on https://github.com/sakehl/Halide/tree/annotated_halide. It is also included in the `dependencies.zip` file (under `Halide`).

## Setup VM
1. First [download](https://zenodo.org/records/7113223) the Tacas 23 AE VM. 
2. Import this VM using VirtualBox. 
3. [Download](https://doi.org/10.5281/zenodo.10041125) this artifact to a folder (not on the VM).
4. Add a `shared folder` in VirtualBox pointing towards this folder. As mount point choose `/home/tacas23/haliverexperiments`. Tick the `auto-mount` option.
5. Start up the VM, the password is `tacas23`.
6. Inside the folder `/home/tacas23/haliverexperiments` open a terminal.
7. Run `sudo ./install.sh`
   
   When stuck, you can always run `./clean.sh` from the folder to clean up some things.
8. This should have installed everything.
   - HaliVer/Halide should be installed at `/haliver`
   - VerCors should be located at `/vercors`. This is a symbolic link, the actual compiled VerCors still resides in the shared folder.
   
### Check Setup
1. In the folder `/home/tacas23/haliverexperiments` start up the `TestSetup.ipynb` jupyter notebook.
2. Run all the cells (`Kernel > Restart & Run All`).
3. Read the comments indicating what happens, no errors should occur when running everything.

## File and Folder structure
- `build` : After using `cmake` to build, contains the files actually verified by VerCors, such as `blur_0.c` and `blur_front.pvl`
- `camera_pipe` : Here are the files used in `Camera_pipe.ipynb`.
- `logs` : After running the experiments, contains the actual output of VerCors for each file. If a verification failure or error occurred, look here for precise output.
- `results` : Results of runs of the Experiments saved as json files. `2023-10-09-18-59_line_info.json` and `2023-10-09-18-59_results_verification.json` are the files on which the paper is based.
- `src` : The Halide source files for the Experiments of Section 4 of the paper.
- `test` : Test files are written here after running all the cells of `TestSetup.ipynb`.
- `tutorial` : A tutorial for how to use HaliVer on arbitrary Halide programs.
- `camera_pipe.ipynb` : In Section 4 of the paper we talk about `camera_pipe` not verifying. We say that the annotations are not incorrect, but the program is to big for the underlying tools. In this notebook we show this.
- `clean.sh` : Bash script that cleans up some files
- `CMakeLists.txt` : CMake instructions
- `dependencies.zip` Dependencies for this artifact (Ubuntu packages, Scala packages, VerCors and HaliVer/Halide)
- `install.sh` : Script that installs the dependencies, should run with `sudo`.
- `LICENSE.txt` : License
- `preprocess.py` : Python file which contains some easier ways to conduct the experiments and process the data of them to tables. Used in the Jupyter notebooks.
- `README.md` : This file
- `RunExperiments.ipynb` : Jupyter notebook that runs the experiments of Section 4 of the paper.
- `table.tex` : Base file to generate Table 1 and Table 2 of the paper.
- `TestSetup.ipynb` : Jupyter notebook that tests if everything is set-up correctly.


## Reproducing Results of Paper

### Table 1 and Table 2 of the Paper
Running the full set of experiments should run for about 2 hours per iteration. In the paper we did 5 iterations, to see if the results are consistent and averaging the runtime. We suggest to add enough CPUs for the VM (our machine used 8) and run 1 iteration of the experiments.

1. In the folder `/home/tacas23/haliverexperiments` start up the `RunExperiments.ipynb` jupyter notebook.
2. Run all the cells (`Kernel > Restart & Run All`).
3. After everything has run, you can inspect the file `table.pdf` which should show you Table 1 and Table 2 of the paper.

### Section 4: `camera_pipe`` experiment failing
In Section 4 on the memory safety results, we talk about the camera_pipe experiment. We say that the annotations are not incorrect, but the program is to big for the underlying tools. 
1. In the folder `/home/tacas23/haliverexperiments` start up the `camera_pipe.ipynb` jupyter notebook.
2. Run all the cells (`Kernel > Restart & Run All`).
3. After everything has run, you can inspect the result.

### Tutorial
We also made a tutorial of how to use the tool for your own Halide programs. This can be found under `tutorial/lesson1.cpp` up to `tutorial/lesson3.cpp`.