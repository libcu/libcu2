# dmkdir - make directories

## Synopsis

```
dmkdir [-p] directory ...
```

## Description

Creates each directory with mode `0666`. With `-p`, missing parent components are created first and an existing directory is not an error. A trailing `/` on a name is ignored.

## Options

| option | meaning |
| --- | --- |
| `-p` | create parent directories as needed |

## Exit status

Exits 0 when every directory was created, 1 when one could not be. An option other than `-p` is a usage error and exits 1.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
