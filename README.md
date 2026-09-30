# libcu

The CU standard library, or libcu, is the standard library for the C programming language implemented in CUDA, as specified in the ANSI C standard. It gives device code `stdio`, `stdlib`, `string`, `ctype`, `time`, `dirent`, `unistd`, `fcntl`, `regex`, `grp` and `pwd` with the original names, an in-memory device file system, and Sentinel, the message bus that lets a running kernel call into the host.

This repository is the second generation of [libcu/libcu](https://github.com/libcu/libcu): the same code base, brought up to current toolkits. It builds with CUDA 12 and CUDA 13, C++17, on Linux and Windows, from one CMake project.

## Quick start

Sentinel has to be initialized before device code touches anything on the host, and shut down at the end. An error tells you to start Sentinel if such access is attempted first.

```
#include <cuda_runtimecu.h>
#include <sentinel.h>
...
// choose which GPU to run on and extend the stack (optional)
cudaErrorCheck(cudaSetDevice(gpuGetMaxGflopsDevice()));
cudaErrorCheck(cudaDeviceSetLimit(cudaLimitStackSize, 1024 * 5));
sentinelServerInitialize();
// sentinelRegisterFileUtils(); // if the d* file utilities should reach this process
...
sentinelServerShutdown();
```

Standard headers are included with the original name suffixed by `cu`, and the functions keep their names:

```
#include <stdlibcu.h>
...
int val = atoi("123");
```

Paths that start with `:` live in the device file system; any other path is forwarded to the host through Sentinel.

## Building

Requirements: CMake 3.24 or newer, a CUDA toolkit 12.x or 13.x, and a C++17 host compiler (GCC 11+, Clang 14+, Visual Studio 2022).

```
cmake --preset default        # Debug, sm_75; builds without a GPU present
cmake --build --preset default
ctest --test-dir build/default -L host      # tests that need no GPU
ctest --test-dir build/default -L gpu       # tests that need one

cmake --preset native         # Release for the GPU in this machine
cmake --preset tcl            # also the Tcl interpreter ports
cmake --preset windows        # Visual Studio 2022, x64
```

`CMAKE_CUDA_ARCHITECTURES` picks the compute capability. The default is `75` because CUDA 13 dropped everything below it; use `native` on a machine with a GPU, or a list such as `75;86;90`.

Targets:

| target | what it is |
| --- | --- |
| `libcu` | the device library and the Sentinel server; link this from your CUDA program |
| `libcu.fileutils` | the device side of the file utilities, and the executor that serves the `d*` tools |
| `dcat`, `dls`, `dcp`, ... | fifteen host programs, one per shell command, that run the command on the device file system of a running libcu process over the host bus |
| `libcu_tests` | the test runner; each ctest entry runs one case |
| `sentinel_host_test` | a loopback test of the host bus that runs without a GPU |
| `libcu.jimtcl`, `libcu.jimtcl.78`, `libcu.tinytcl` | the Tcl interpreter device ports, with `jimtcl` and `tinytcl` shells (off by default) |

Using libcu from another CMake project: `add_subdirectory(libcu2)` and `target_link_libraries(app PRIVATE libcu::libcu)`. Both projects need `CUDA_SEPARABLE_COMPILATION` on, which the target carries with it.

## Layout

```
src/include            the headers: <stdiocu.h>, <sentinel.h>, ext/, sys/
src/libcu              the device library (libcu.cu is a unity build of the .cu files beside it)
src/libcu.fileutils    device implementations of ls, cat, cp, ... and the d* front ends
src/libcu.tests        the test runner and the host loopback test
src/tcl                jimtcl 0.77, jimtcl 0.78 and tinytcl ported to run on the device
docs                   the reference: docs/sentinel.md and docs/fixed-assets.md explain the design
```

## Documents

This project follows the standard libc interface.
* Learning by reference: documentation can be found in [docs](docs).
* Learning by tests: tests can be found in [libcu.tests](src/libcu.tests).

## Contributing

If you would like to contribute to libcu to help the project along, consider these options:
* Improve the [documentation](docs). Documentation often gets overlooked, but can be a large contributor to the success of a project.
* Submit a [bug report](https://github.com/libcu/libcu2/issues) (for an excellent guide on submitting good bug reports, read [Painless Bug Tracking](https://www.joelonsoftware.com/2000/11/08/painless-bug-tracking/)).
* Submit a [feature request](https://github.com/libcu/libcu2/issues).
* Help verify submitted fixes for bugs.
* Help answer questions in the discussions list.
