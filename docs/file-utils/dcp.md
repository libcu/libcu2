# dcp - copy files

## Synopsis

```
dcp source target\ndcp source ... directory
```

## Description

Copies `source` to `target`. When the last argument is a directory, every source is copied into it under its own base name. Copying a file onto itself is refused.

The copy is made on the device with 1 KB reads and writes, so files may be copied between the device file system and the host in either direction. Permission bits, owner and times are not carried over.

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
