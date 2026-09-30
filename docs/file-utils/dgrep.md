# dgrep - search files for a string

## Synopsis

```
dgrep [-i] [-n] string file ...
```

## Description

Prints every line of each file that contains `string`. The match is a plain substring, not a regular expression. With more than one file each line is prefixed with the file name.

## Options

| option | meaning |
| --- | --- |
| `-i` | ignore case |
| `-n` | prefix each line with its line number |

## Exit status

Exits 0. An unknown option exits 1. A file that cannot be opened is reported with `perror`. Lines longer than 8191 characters are reported as `Line too long`.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
