# dls - list directory contents

## Synopsis

```
dls [-ladiFA] [name ...]
```

## Description

Lists the files named, or the current device directory when none is given. Directories are listed by content unless `-d` is given. Entries are sorted by name and, when not in long format, laid out in columns.

## Options

| option | meaning |
| --- | --- |
| `-l`, `-g` | long format: mode string, link count, owner, group, size, modification time, name |
| `-d` | list a directory itself rather than its contents |
| `-i` | print the inode number first |
| `-a` | include entries whose name starts with `.` |
| `-F` | classify: append `/` to directories and `*` to executables |
| `-A` | accepted and ignored |

In the lean build of libcu the owner, group and time columns of the long format show placeholders, because the password, group and `ctime` functions are left out.

## Exit status

Exits 0. An unknown option exits 1. Names that cannot be listed are reported and skipped.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
