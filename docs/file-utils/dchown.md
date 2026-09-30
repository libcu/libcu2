# dchown - change file owner

## Synopsis

```
dchown user file ...
```

## Description

Sets the owner of each file to `user`, which is either a numeric user id or a user name. A name is looked up on the device with `getpwnam`. The group is left unchanged.

## Exit status

Exits 1 when the user is not numeric and unknown, or when the id has trailing characters. Files that cannot be changed are reported with `perror`; the exit status is otherwise 0.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
