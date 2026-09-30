# dchmod - change file mode

## Synopsis

```
dchmod mode file ...
```

## Description

Sets the permission bits of each file to `mode`, given in octal (for example `644` or `0755`). Symbolic modes are not supported.

## Exit status

Exits 1 when the mode is not octal. Files that cannot be changed are reported with `perror`; the exit status is otherwise 0.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
