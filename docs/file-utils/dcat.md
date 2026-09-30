# dcat - concatenate files

## Synopsis

```
dcat [file ...]
```

## Description

Writes each named file to standard output, in order. With no arguments it copies its own standard input to standard output without involving the device.

Files are read on the device with `fopen`/`fread`, so they may live in the device file system or on the host.

## Exit status

Exits 0. A file that cannot be opened is reported on standard error with its `errno` text.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
