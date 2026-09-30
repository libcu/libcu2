# dcd - change the device working directory

## Synopsis

```
dcd [directory]
```

## Description

Changes the working directory of the device file system, which is what relative device paths in later commands resolve against.

Without an argument, or with more than one, it prints the current device directory instead (see `dpwd`).

## Exit status

Exits 0 on success and 1 when the directory cannot be entered.

The device side of this command is currently a stub: it accepts the request and returns success without changing the directory. Only `dpwd` is functional today.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
