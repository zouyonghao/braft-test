# export CC=/home/zyh/distributed-system-test/build/fuzz/default_compiler
# export CXX=/home/zyh/distributed-system-test/build/fuzz/default_compiler++

# cp /home/zyh/distributed-system-test/configs/compiler-config.json /tmp/
# cp /home/zyh/distributed-system-test/build/fuzz/CMakeFiles/default_instrument_lib.dir/instrumentor_lib/default_instrument_lib.c.o /tmp

# export CC="clang-9"
# export CXX="clang++-9"
export CC=gcc
export CXX=gcc

rm -rf build

mkdir build
cd build
cmake ..
make -j12

cd ../example/atomic

rm -r CMakeFiles CMakeCache.txt cmake_install.cmake

cmake .
make clean
make
