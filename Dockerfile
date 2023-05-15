FROM jupyter/minimal-notebook

WORKDIR /experiments

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

USER root

RUN apt update && apt install -y --no-install-recommends \
           ca-certificates build-essential wget git unzip \
           cmake clang-11 ninja-build zlib1g-dev llvm-11-dev \
           libclang-11-dev liblld-11 liblld-11-dev

# Get HaliVer/Halide
RUN cd /tmp && git clone https://github.com/sakehl/Halide.git && \
    cd Halide && git checkout annotated_halide && git checkout c9989ea35fea8b26f18da90eef1294d3dc0223af

# Build HaliVer/Halide
RUN cd /tmp/Halide && mkdir build && \
    cmake -G Ninja \
    -DWITH_TESTS=NO -DWITH_AUTOSCHEDULERS=NO -DWITH_PYTHON_BINDINGS=NO -DWITH_TUTORIALS=NO -DWITH_DOCS=NO -DCMAKE_BUILD_TYPE=Release \
    -DTARGET_AARCH64=NO -DTARGET_AMDGPU=NO -DTARGET_ARM=NO -DTARGET_HEXAGON=NO -DTARGET_MIPS=NO -DTARGET_NVPTX=NO -DTARGET_POWERPC=NO \
    -DTARGET_RISCV=NO -DTARGET_WEBASSEMBLY=NO \
    -S . -B build && cmake --build build

# Install HaliVer/Halide
RUN mkdir /haliver && cmake --install /tmp/Halide/build --prefix /haliver

# Install pre-requisites for VerCors
RUN apt install -y openjdk-17-jre-headless

# Get VerCors
RUN mkdir /vercors && fix-permissions /vercors && git config --global --add safe.directory /vercors

USER $NB_UID

RUN git clone https://github.com/sakehl/vercors.git /vercors \
    && cd /vercors && git checkout haliver && git checkout 6c8d08fa04809463a6b80e059e081299aed71b13

USER root
RUN apt install -y curl
USER $NB_UID

RUN cd /vercors && ./mill vercors.compile

ENV PATH="$PATH:/vercors/bin"

USER root

COPY src src

COPY RunExperiments.ipynb RunExperiments.ipynb
COPY preprocess.py preprocess.py
COPY CMakeLists.txt CMakeLists.txt
COPY CTestConfig.cmake CTestConfig.cmake
COPY table.tex table.tex

COPY results/ results/

RUN mkdir -p build && mkdir -p results

RUN fix-permissions /experiments && fix-permissions /experiments/results

USER $NB_UID

RUN cmake -G Ninja -S . -B build