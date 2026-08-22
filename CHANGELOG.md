# Changelog

All notable changes to FunsCore / FunsOS are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.8.4] - 2026-08-22

### Highlights

26 shell commands previously shipped as stubs have been rewritten with real
implementations. They are exercised end-to-end against the existing VFS, network
stack, log buffer and timer subsystems, and add no new kernel dependencies.

### Added - Text Processing (`kernel/cmd_text.c`, 1322 lines)

| Command   | Description                                                   |
|-----------|---------------------------------------------------------------|
| `cut`     | Extract selected fields / character ranges from each line      |
| `paste`   | Merge lines from multiple files side-by-side (TAB separated)  |
| `tr`      | Translate / delete characters (`\t`, `\n`, `\\` supported)     |
| `rev`     | Reverse each line                                              |
| `fold`    | Wrap each line to a given width (`-w N`, `-s` to break at space) |
| `expand`  | Convert leading TABs to spaces (`-t N`)                       |
| `unexpand`| Inverse of `expand`                                            |
| `nl`      | Number non-empty lines                                         |
| `look`    | Display lines beginning with a prefix                          |
| `comm`    | Three-column compare of two sorted files                       |
| `tsort`   | Topological sort (Kahn algorithm, cycle detection)            |

All commands use `vfs_open`/`vfs_read` and report clear errors on missing files.

### Added - Data Manipulation (`kernel/cmd_data.c`, 766 lines)

| Command   | Description                                                   |
|-----------|---------------------------------------------------------------|
| `dd`      | Copy / convert files with `if=`/`of=`/`bs=`/`count=`/`skip=`/`seek=` |
| `split`   | Split a file into pieces of N lines (`split -l N FILE PREFIX`) |
| `join`    | Join two files on the first TAB-separated field               |
| `hexdump` | Canonical hex+ASCII dump (`-C`) with optional `-n LENGTH`     |
| `strings` | Extract printable strings (`-n MIN`, `-o OFFSET`)             |
| `cksum`   | POSIX CRC32 over multiple files                               |

### Added - Integrity Checksum (`kernel/cmd_hash.{h,c}`)

| Command     | Description                                                   |
|-------------|---------------------------------------------------------------|
| `sha256sum` | SHA-256 digest of one or more files; supports `-c` verify mode |
| `md5sum`    | RFC 1321 MD5 digest; supports `-c` verify mode                |
| `hash`      | Unified dispatcher: `hash sha256sum FILE...` / `hash md5sum FILE...` |

The MD5 implementation is self-contained in `cmd_hash.c` (no external crypto
dependency) and follows RFC 1321 byte-for-byte.

### Added - System Logging (`kernel/cmd_log.c`, 366 lines)

| Command     | Description                                                   |
|-------------|---------------------------------------------------------------|
| `dmesg`     | Print kernel log buffer; `-c` clears after display            |
| `loglevel`  | Get / set log verbosity (`info` / `warn` / `err` / `debug`)   |
| `syslog`    | View / rotate / clear / dump-to-serial kernel log             |

All operations bind to the existing `klog.h` API — no new logging code paths.

### Added - Network Diagnostics (`kernel/cmd_netdiag.c`, 461 lines)

| Command     | Description                                                   |
|-------------|---------------------------------------------------------------|
| `wol`       | Send a 102-byte Wake-on-LAN magic packet (broadcast 255.255.255.255:9, falls back to `/tmp/wol_packet.bin`) |
| `speedtest` | TCP loopback throughput test against `127.0.0.1:8080`         |
| `iptraf`    | Per-interface traffic counters (RX/TX packets/bytes/errors)   |
| `ftp`       | Placeholder; will arrive when the TCP stack matures           |

### Changed

- `kernel/version.h`: bumped `KERNEL_VERSION` from `0.8.3` → `0.8.4`
- `kernel/cmd_all.c`: removed the `cmd_hash` stub (real implementation now in `cmd_hash.c`)
- `kernel/shell.c`: registered `sha256sum` and `md5sum` as top-level commands
  and added the `cmd_hash` dispatcher route via `kernel/cmd_hash.h`

### Notes

- All new commands share the existing `shell_print` / `shell_last_exit_code`
  conventions, so `set -e` style shell scripts behave as expected.
- No new `.h` files were introduced except `kernel/cmd_hash.h` (7 lines).
- No driver, syscall or filesystem ABI was changed; v0.8.4 is a pure
  shell-side upgrade and is safe to back-deploy.

---

## [0.8.3] - 2026-07-xx

- Initial public release of FunsCore / FunsOS.
- Boot, kernel, scheduler, VFS, log buffer, network stack, basic shell.