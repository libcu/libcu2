# File utilities

Fifteen host programs, one per shell command, that run the command on the device file system of a running libcu process. Each is a small C++ program that includes the Sentinel client and talks to the server over the host bus; the server runs the command in a kernel through `libcu.fileutils`.

| tool | command |
| --- | --- |
| [dcat](dcat.md) | concatenate files |
| [dcd](dcd.md) | change the device working directory |
| [dchgrp](dchgrp.md) | change file group |
| [dchmod](dchmod.md) | change file mode |
| [dchown](dchown.md) | change file owner |
| [dcmp](dcmp.md) | compare two files |
| [dcp](dcp.md) | copy files |
| [dgrep](dgrep.md) | search files for a string |
| [dls](dls.md) | list directory contents |
| [dmkdir](dmkdir.md) | make directories |
| [dmore](dmore.md) | page through files |
| [dmv](dmv.md) | move or rename files |
| [dpwd](dpwd.md) | print the device working directory |
| [drm](drm.md) | remove files |
| [drmdir](drmdir.md) | remove empty directories |

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.

## Building

The tools are part of the default CMake preset (`LIBCU_BUILD_FILEUTILS`, on by default) and are built as `d<name>` next to `libcu.a`. They link no device code, only the headers, so they build quickly and run anywhere the server runs.

## Adding a tool

Each tool is three pieces:

- `src/libcu.fileutils/d<name>.cuh`: the device side, a `__global__` kernel and a host wrapper `d<name>(pipelineRedir, ...)` that copies the arguments to the device, launches it and reads the result back.
- `src/libcu.fileutils/d<name>.cpp`: the front end that parses arguments and sends a `fileutils_d<name>` message.
- `src/libcu.fileutils/sentinel-fileutilsmsg.h` and `sentinel-msg.cpp`: the message struct and the case in `sentinelFileUtilsExecutor` that calls the wrapper.

Add the name to `LIBCU_TOOLS` in `CMakeLists.txt` and include the `.cuh` from `libcu.fileutils.cu`. See `docs/sentinel.md` for how messages embed strings and larger buffers.
