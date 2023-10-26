#/bin/bash

if [ "$(id -u)" -ne 0 ]; then
        echo 'This script must be run by root' >&2
        exit 1
fi

unzip dependencies.zip

# Install all packages needed
cd dependencies/packages
dpkg -i *.deb
dpkg -i python3-minimal_3.10.6-1~22.04_amd64.deb
dpkg -i python3_3.10.6-1~22.04_amd64.deb
apt --fix-broken install

# VerCors needs clang, but we already have 11 installed, so lets use that
ln -s /usr/bin/clang-11 /usr/bin/clang

# These maven dependencies were already downloaded beforehand, so offline compilation works
cd ..
sudo -u tacas23 rsync -a coursier /home/tacas23/.cache

# Compile VerCors
cd vercors
sudo -u tacas23 git config --global --add safe.directory $(pwd)/out/foreign-modules/project/fetchViper/silverGit/repo.dest
sudo -u tacas23 git config --global --add safe.directory $(pwd)/out/foreign-modules/project/fetchViper/siliconGit/repo.dest
sudo -u tacas23 git config --global --add safe.directory $(pwd)/out/foreign-modules/project/fetchViper/carbonGit/repo.dest

sudo -u tacas23 ./mill --no-server vercors.compile
# Put VerCors on the PATH
ln -s $(pwd)/bin /vercors
export PATH=$PATH:/vercors
echo "export PATH=\$PATH:/vercors" >> /home/tacas23/.bashrc

# Build HaliVer/Halide
cd ../haliver
mkdir -p /home/tacas23/haliver-build
mkdir -p /haliver

cmake -G Ninja \
    -DWITH_TESTS=NO -DWITH_AUTOSCHEDULERS=NO -DWITH_PYTHON_BINDINGS=NO -DWITH_TUTORIALS=NO -DWITH_DOCS=NO \
    -DCMAKE_BUILD_TYPE=Release -DTARGET_AARCH64=NO -DTARGET_AMDGPU=NO -DTARGET_ARM=NO -DTARGET_HEXAGON=NO \
    -DTARGET_MIPS=NO -DTARGET_NVPTX=NO -DTARGET_POWERPC=NO -DTARGET_RISCV=NO -DTARGET_WEBASSEMBLY=NO \
    -S . -B /home/tacas23/haliver-build

cmake --build /home/tacas23/haliver-build

# Install HaliVer/Halide
cmake --install /home/tacas23/haliver-build  --prefix /haliver

cd ../..
sudo -u tacas23 rm -r build 
sudo -u tacas23 mkdir -p build 
sudo -u tacas23 cmake -S . -B build
sudo -u tacas23 cmake --build build
