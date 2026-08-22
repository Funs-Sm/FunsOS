# Changelog

All notable changes to FunsCore / FunsOS are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.8.6] - 2026-08-22

### Highlights

Tier-2 / Tier-3 / Tier-4 shell stubs in `kernel/cmd_all.c` reach the real
backend.  v0.8.6 wires three new modules into the shell dispatcher and
extends the routing table with `perf` and `stacktrace`.

### Added - User utilities (`kernel/cmd_utility.{c,h}`, ~270 lines)

| Command          | Description                                                          |
|------------------|----------------------------------------------------------------------|
| `which CMD`      | locate a builtin or scan `/bin` `/usr/bin` `/sbin` etc.             |
| `type CMD`       | report builtin / alias / file (same data as `which`)                 |
| `tee [-a] FILE`  | touch the named files in write or append mode                       |
| `xargs [-n N]`   | print the would-be invocation (no stdin yet)                        |
| `test EXPR`      | POSIX primaries: `-z` `-n` `-e` `-f` `-d` and `=`/`!=`/`-eq`/...     |
| `expr EXPR`      | integer arithmetic with `+ - * / %`                                  |
| `install SRC DST`| copy with optional mode (`-m OCTAL`)                                 |

### Added - Process control (`kernel/cmd_procctl.{c,h}`, ~150 lines)

| Command           | Description                                                          |
|-------------------|----------------------------------------------------------------------|
| `nice -n DELTA PID`  | adjust a process's `sched_set_priority()`                       |
| `renice -n DELTA PID`| POSIX-style renice                                             |
| `nohup CMD`        | explicitly reports that background invocation needs fork/exec        |
| `jobs` `bg` `fg`   | honestly report single-tasked shell (no job table)                   |

### Added - Kernel debug (`kernel/cmd_kdebug.{c,h}`, ~150 lines)

| Command         | Description                                                          |
|-----------------|----------------------------------------------------------------------|
| `perf [start|stop|reset|tsc]` | wrap `perf.h` APIs                           |
| `stacktrace [N]` | save and print the current call stack via `kernel/stacktrace.h`     |
| `ktrace [clear|on MASK|off MASK]` | wrap `ktrace.h` (show stats, clear, toggle)        |
| `tracepoint [on NAME|off NAME|stats]` | wrap `tracepoint.h`                            |

### Changed

- `kernel/version.h`: bumped `KERNEL_VERSION` 0.8.5 -> 0.8.6
- `kernel/syncstat.h` / `syncstat.c`: not changed (out of scope)
- `kernel/shell.c`:
  - added `#include "cmd_utility.h"`, `#include "cmd_procctl.h"`,
    `#include "cmd_kdebug.h"`
  - **promoted** `current_dir` to non-static; added `shell_current_dir`
    alias for use by external `cmd_*` translation units (existing in-header
    extern was previously dangling).
  - added `perf` and `stacktrace` routes (previously only `dumpstack`).
- `kernel/cmd_all.c`: removed 15 stubs whose real implementations now
  live in `cmd_utility.c`, `cmd_procctl.c`, `cmd_kdebug.c`.  Externs
  remain in `cmd_all.h` for back-compat.

### Backed by

- `kernel/sched.h`: `sched_set_priority(pcb_t, uint32_t)` drives `nice`
  / `renice`.
- `kernel/process.h`: `process_get_pcb(pid_t)` for the priority target.
- `kernel/perf.h` / `kernel/stacktrace.h` / `kernel/ktrace.h` /
  `kernel/tracepoint.h`: existing subsystems.

### Honest limitations

- `bg`/`fg`/`jobs` print that FunsOS is single-tasked; no job table.
- `tee`/`xargs` are no-ops beyond the file-touching semantics because
  the shell has no TTY.
- `time CMD` (v0.8.5) cannot fork a child; only dispatch cost is shown.

### QEMU smoke test

Boot to splash->shell in ~8 s, no regression versus v0.8.5.

---

## [0.8.5] - 2026-08-22

### Highlights

Tier-1 shell commands that already had full backing subsystems shipped only
as stubs in `kernel/cmd_all.c`.  v0.8.5 wires them to the real implementations
(`acpi_reboot`, `acpi_shutdown`, `timer_sleep`, `timer_get_ticks`).

### Added - Power management (`kernel/cmd_power.{c,h}`, 110 lines)

| Command    | Description                                                   |
|------------|---------------------------------------------------------------|
| `reboot`   | ACPI reset register with 5-second countdown (`-f` to skip)      |
| `halt`     | CPU `cli; hlt` loop (power stays on)                          |
| `shutdown` | ACPI S5 (soft off) with keyboard-controller fallback           |
| `poweroff` | Alias of `shutdown`                                            |

### Added - Time utilities (`kernel/cmd_time.{c,h}`, 184 lines)

| Command   | Description                                                   |
|-----------|---------------------------------------------------------------|
| `sleep`   | Pause the shell; accepts `5`, `5s`, `250ms`, `2m`, `1h`         |
| `watch`   | Print a banner every N seconds (default 2 s, `-n SECONDS`)     |
| `time`    | Print uptime (`HH:MM:SS` and raw tick count)                  |
| `time CMD ARG...` | Measure elapsed ticks around a command (best-effort)   |

Note: `time CMD` cannot yet fork a child; it measures its own dispatch cost
and clearly labels the limitation. True per-child accounting waits for v0.9.

### Changed

- `kernel/version.h`: bumped `KERNEL_VERSION` 0.8.4 -> 0.8.5
- `kernel/cmd_all.c`: removed 7 stubs (`cmd_reboot`, `cmd_halt`, `cmd_shutdown`,
  `cmd_sleep`, `cmd_watch`, `cmd_time`, `cmd_time_cmd`); externs remain in
  `cmd_all.h` for back-compat
- `kernel/shell.c`:
  - added `#include "cmd_power.h"`, `#include "cmd_time.h"`
  - added `poweroff` alias route
  - fixed pre-existing bug: `time CMD ARG...` now actually passes the joined
    `full_cmd` to `cmd_time_cmd` (previously it called `cmd_time_cmd(arg)` and
    discarded the local concatenation)
  - `time` (no args) now calls `cmd_time(NULL)` instead of `cmd_time(arg)` for
    a clean uptime-only print

### Backed by

- `kernel/acpi_sleep.{c,h}` already exposed `acpi_reboot()` / `acpi_shutdown()`
  / `acpi_enter_sleep(state)`; v0.8.5 simply routes the shell commands there.
- `kernel/timer.{c,h}` already exposed `timer_sleep(ms)` / `timer_get_ticks()`
  at 100 Hz; v0.8.5 uses them for sleep / watch / time.

### Notes

- All new commands follow the v0.8.4 style: `shell_print` + `shell_last_exit_code`.
- No new ABI, syscall, or filesystem change.
- `cmd_watch` is honest about being single-tasked: it only prints banners.
  A real dispatcher loop needs the v0.9 task scheduler.

### QEMU smoke test

Boot with `-drive format=raw,file=os.img -m 128 -nographic`. Splash → shell
in ~8 s, no panic, no regression versus v0.8.4.

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