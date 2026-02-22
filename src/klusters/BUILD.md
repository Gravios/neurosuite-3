# Building klusters

## Standard build (OpenMP only)

```sh
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

## With Intel Arc / SYCL acceleration

CMake must be told to use `icpx` as the CXX compiler **before** the cache is
created.  Do this once from a clean directory:

```sh
source /opt/intel/oneapi/setvars.sh
mkdir build && cd build
cmake .. -DCMAKE_CXX_COMPILER=$(which icpx)
make -j$(nproc)
sudo make install
```

Passing `-DCMAKE_CXX_COMPILER=icpx` on the first run avoids the
"variables that require your cache to be deleted" double-configure that
occurs if cmake switches the compiler mid-run.

If you have an existing build directory and want to enable SYCL, delete it
first:

```sh
rm -rf build
source /opt/intel/oneapi/setvars.sh
mkdir build && cd build
cmake .. -DCMAKE_CXX_COMPILER=$(which icpx)
make -j$(nproc)
```

## Disabling GPU backends explicitly

```sh
cmake .. -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
```

## Backend selection at runtime

The xcorr realignment backend is selected automatically at startup in priority
order: CUDA → HIP → SYCL → OpenMP.  The chosen backend is shown in the
pre-flight Realign Spikes dialog.
