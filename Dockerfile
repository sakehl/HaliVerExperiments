FROM jupyter/minimal-notebook

WORKDIR /experiments

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

USER root

RUN apt update && apt install -y --no-install-recommends \
           ca-certificates build-essential curl git wget unzip \
           cmake clang-11 ninja-build zlib1g-dev llvm-11-dev \
           libclang-11-dev liblld-11 liblld-11-dev \
           openjdk-17-jre-headless

# Get VerCors
RUN mkdir /vercors && fix-permissions /vercors && git config --global --add safe.directory /vercors

USER $NB_UID

RUN git clone -b 'haliver-v1.1' --single-branch --depth 1 https://github.com/sakehl/vercors.git /vercors

RUN cd /vercors && ./mill vercors.compile

ENV PATH="$PATH:/vercors/bin"

USER root

# Get HaliVer/Halide
RUN cd /tmp && git clone -b 'haliver-v1.1' --single-branch --depth 1 https://github.com/sakehl/Halide.git

# Build HaliVer/Halide
RUN cd /tmp/Halide && mkdir build && \
    cmake -G Ninja \
    -DWITH_TESTS=NO -DWITH_AUTOSCHEDULERS=NO -DWITH_PYTHON_BINDINGS=NO -DWITH_TUTORIALS=NO -DWITH_DOCS=NO -DCMAKE_BUILD_TYPE=Release \
    -DTARGET_AARCH64=NO -DTARGET_AMDGPU=NO -DTARGET_ARM=NO -DTARGET_HEXAGON=NO -DTARGET_MIPS=NO -DTARGET_NVPTX=NO -DTARGET_POWERPC=NO \
    -DTARGET_RISCV=NO -DTARGET_WEBASSEMBLY=NO \
    -S . -B build && cmake --build build

# Install HaliVer/Halide
RUN mkdir /haliver && cmake --install /tmp/Halide/build --prefix /haliver

COPY src src

COPY RunExperiments.ipynb RunExperiments.ipynb
COPY preprocess.py preprocess.py
COPY CMakeLists.txt CMakeLists.txt
COPY CTestConfig.cmake CTestConfig.cmake
COPY table.tex table.tex

COPY results/ results/

RUN mkdir -p build && mkdir -p results && mkdir -p logs

RUN fix-permissions /experiments && fix-permissions /experiments/results && fix-permissions /experiments/logs

USER $NB_UID

RUN cmake -G Ninja -S . -B build
