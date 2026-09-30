# dmore - page through files

## Synopsis

```
dmore file ...
```

## Description

Shows each file a screen at a time: 24 lines of 80 columns, then the prompt `--More--`. Each file is announced as `<< name >>`.

At the prompt, a line read from the tool's standard input controls what happens next:

| key | action |
| --- | --- |
| Enter (anything else) | show the next screen |
| `n` | skip to the next file |
| `q` | quit |

A leading `:` is ignored, so `:n` and `:q` also work.

## Exit status

Exits 0. A file that cannot be opened is reported with `perror` and skipped.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
