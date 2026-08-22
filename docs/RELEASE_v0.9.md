# Release Notes — FunsOS v0.9

> Big-jump release — FunsCore kernel has been upgraded from 0.8.7
> straight to 0.9, skipping 0.8.8 / 0.8.9.  This release contains
> roughly two months of accumulated subsystem work plus the long-
> standing safety hardening that never quite landed in 0.8.x.

## 亮点

- **文件系统** — xattr (4 个 namespace) + quota (soft/hard/grace) + io_uring + path hash + icache hit-rate improvement
- **网络栈** — ICMPv6 + PMTUD + TCP SACK 公开 helper
- **调度器** — lib/rbtree (Cormen) + CFS 用 SACK-aware retransmit
- **信号** — SA_RESTORER 标志 + 默认 sigreturn 兜底
- **Shell** — 5 个新 `cmd_*` 命令 (`xattr` / `acpi` / `signal` / `rbtree` / `io`)
- **Socket 选项** — `SO_TIMESTAMP` / `SO_TIMESTAMPNS` / `SO_TIMESTAMPING` 通过 `setsockopt`/`getsockopt` 操作 `s->ts_flags` bitfield

## 资产

| 资产 | 说明 |
|------|------|
| `os.img` | 引导磁盘镜像：1 MiB BIOS 区域 + boot/loader + kernel.elf + tarfs 段 |
| `kernel.bin` | flat-binary 形式的 kernel，方便引导装载机写盘 |
| `kernel.elf` | 含符号的 ELF (pei-i386, image-base 0x100000) |
| `kernel.map` | 完整符号表，便于 trap/analyze 还原调用栈 |
| `source.tar.gz` | git archive 出来的源码 (附带 .git 子集) |

## 如何验证 (QEMU)

```bash
qemu-system-i386 -machine accel=tcg -m 256 -drive format=raw,file=build/os.img -serial stdio
```

启动后进入 kshell，输入 `help` 列命令，`dmesg` 看内核日志，`rbtree` 看 lib/rbtree 统计。

## 已知问题

参见 `docs/ROADMAP.md` 的 1.0 章节。
