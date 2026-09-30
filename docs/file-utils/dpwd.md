# dpwd - print the device working directory

## Synopsis

```
dpwd
```

## Description

Prints the working directory of the device file system, as `getcwd` on the device reports it. The initial value is the root, `:\`.

## Exit status

Exits 0, or 1 with `pwd: cannot get current directory` when the device cannot answer.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
