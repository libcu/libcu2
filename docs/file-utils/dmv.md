# dmv - move or rename files

## Synopsis

```
dmv source target\ndmv source ... directory
```

## Description

Renames `source` to `target`. When the last argument is a directory, every source is moved into it under its own base name.

The rename is attempted first. When it fails because the two paths are on different file systems (`EXDEV`, for example moving between the device file system and the host), the file is copied with its modes and the source is removed.

## Exit status

Exits 1 when more than one source is given and the last argument is not a directory; otherwise 0. Individual failures are reported with `perror`.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
