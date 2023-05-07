# HaliVerExperiments
These are the experiments conducted with HaliVer. The easiest way to run them is with Docker. 

## Run with Docker
First do
```bash
docker build . -t haliver
```
to build the docker image

Next do
```bash
docker run -p 8888:8888 -v "$(pwd)"/results:/experiments/results haliver
```
to run the docker image. Open the jupyter lab link that appears in the terminal. Follow the instructions of the `RunExperiments.ipynb` file to run the experiments and make the table.
The results are saved in the `results` folder, with a time stamp.

### HaliVer
The HaliVer tool itself is a fork of the Halide compiler and can be found on https://github.com/sakehl/Halide/tree/annotated_halide