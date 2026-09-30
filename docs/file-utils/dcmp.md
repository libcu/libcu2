# dcmp - compare two files

## Synopsis

```
dcmp file1 file2
```

## Description

Compares the two files byte by byte on the device and reports the first difference. Messages are one of:

- `Files are identical`
- `Files are links to each other` (same device and inode)
- `Files are different sizes`
- `First file is shorter than second` / `Second file is shorter than first`
- `Files differ at byte position N`

## Exit status

Exits 0 when the files are identical or the same file, 1 when they differ, and 2 when a file cannot be opened or read.

## Requirements

A libcu program must be running on the same machine with Sentinel started and the file utilities registered:

```
sentinelServerInitialize();
sentinelRegisterFileUtils();
```

The tool connects to that process over the Sentinel host bus (`SENTINEL_NAME`) and the command runs on the device. Paths that begin with `:` name the device file system (`:\` is its root; `/` and `\` both separate components). Any other path is a host path of the server process.

Output produced on the device reaches the tool's console on Windows through the redirection pipeline. On Linux that redirection is not implemented yet, so the output appears on the server's stdout.
