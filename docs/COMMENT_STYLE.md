# FunsOS 注释语言规范 (Comment Style Guide)

> **版本**: 1.0
> **生效**: v0.8.3+ 所有新提交代码
> **维护**: @funsos-team
> **关联**: [NAMING_CONVENTIONS.md](NAMING_CONVENTIONS.md) | [ROADMAP.md](ROADMAP.md)

---

## 1. 总原则

FunsOS 是一个**中文社区教学型操作系统**，注释语言策略的核心目标：

1. **核心内核**：详细中文注释（教学价值优先）
2. **公开 SDK / 头**：英文（国际化 + 用户态引用）
3. **用户态 CLI**：英文（与 Linux userland 习惯一致）
4. **关键路径**：中英双语（教学 + 国际化兼容）

---

## 2. 模块分类规范

| 模块前缀 | 注释语言 | 说明 |
|---|---|---|
| `kernel/*.c` | 中文（详细） | 核心教学模块，逐函数说明 |
| `kernel/*.h` | 中文（API 文档），英文（导出符号声明） | 头文件分两部分 |
| `fs/*.c` | 中文 | 文件系统实现教学价值高 |
| `net/*.c` | 中文 | 网络协议栈教学 |
| `drivers/*.c` | 中文（驱动逻辑），英文（PCI ID / 设备常量） | 混合 |
| `gui/*.c` | 中英双语（关键函数） | GUI 模块复杂度高 |
| `gui/*.h` | 英文（API 签名） | 用户态可能 include |
| `renderer/src/*.c` | 中文（算法说明） | 渲染算法解释 |
| `apps/*_app.c` | 英文 | 内嵌 CLI 工具 |
| `apps/*/main.c` | 英文 | 用户态应用 |
| `sdk/include/*.h` | **必须英文** | 公开 SDK，所有用户引用 |
| `sdk/lib/*` | 英文 | SDK 实现 |
| `sdk/examples/*/main.c` | 英文 | 教学示例，国际通用 |
| `docs/*.md` | 中文为主，英文为辅 | 文档可双语 |

---

## 3. 函数注释模板

### 3.1 内核模块（中文）

```c
/**
 * @brief  创建新进程
 * @param  name      进程名（最长 64 字节）
 * @param  entry     入口函数指针
 * @param  stack_top 栈顶地址（已分配）
 * @return 新进程 PID，失败返回 -1
 *
 * 本函数执行以下操作：
 *   1. 分配 PCB
 *   2. 设置 TSS/EFLAGS
 *   3. 复制父进程文件描述符表
 *   4. 将进程加入就绪队列
 *
 * 注意：必须在中断禁用状态下调用
 */
int process_create(const char *name, void (*entry)(void), void *stack_top);
```

### 3.2 SDK 头文件（英文）

```c
/**
 * @brief Create a new process.
 * @param name      Process name (max 64 bytes).
 * @param entry     Entry function pointer.
 * @param stack_top Stack top address (must be pre-allocated).
 * @return New process PID on success, -1 on failure.
 */
int process_create(const char *name, void (*entry)(void), void *stack_top);
```

---

## 4. 行内注释规范

- 中文注释：`/* 这是一个中文注释 */` 或 `// 这是单行注释`
- 英文注释：`/* This is an English comment */`
- 禁止：方言、俚语、网络用语
- 禁止：在注释中写代码改动历史（用 `git log`）

---

## 5. TODO / FIXME / XXX 规范

- `TODO(<github-username>): 说明` — 计划要做的事
- `FIXME(<github-username>): 说明` — 必须修复的问题
- `XXX(<github-username>): 说明` — 代码可疑或需要重构

**示例：**
```c
/* TODO(@funsos-team): 实现 writeback 缓存 */
int fsync(int fd) { /* FIXME(@funsos-team): 当前仅 sync，缺真正的 writeback */ }
```

---

## 6. 文件头注释模板

每个 `.c` / `.h` 文件应包含：

```c
/*
 * <文件名> - <模块功能简述>
 *
 * <详细说明：模块职责、与其他模块关系、关键算法概述>
 *
 * 作者: <作者> (<年份>)
 * 许可: MIT License (见仓库根 README.md)
 */
```

---

## 7. 注释更新规则

- 修改代码时**必须**同步更新相关注释
- 提交 PR 时注释与代码必须一致
- 注释与代码不一致的代码视为**不通过审查**

---

## 8. 工具检查（可选）

PR 检查脚本 `.github/check_comments.sh`：

```bash
#!/bin/bash
# 检查 sdk/include/*.h 中的中文注释（应为 0）
if grep -P '[\x{4e00}-\x{9fff}]' sdk/include/*.h; then
    echo "ERROR: SDK headers must use English only"
    exit 1
fi

# 检查 kernel/*.c 注释比例（应 ≥ 5%）
for f in kernel/*.c; do
    lines=$(wc -l < "$f")
    comments=$(grep -c '//\|/\*' "$f")
    ratio=$((comments * 100 / lines))
    if [ $ratio -lt 5 ]; then
        echo "WARNING: $f has low comment ratio: ${ratio}%"
    fi
done
```

---

## 9. 例外情况

- **第三方代码**（drivers/ 中参考 Linux 移植的代码）：保留原英文注释
- **测试代码**：注释可以简化
- **构建脚本**（Makefile / .sh）：中文注释允许
- **自动生成代码**：无需注释

---

## 10. 修订历史

| 版本 | 日期 | 修订内容 |
|---|---|---|
| 1.0 | 2026-08-19 | 初版 (PR-6, v0.8.3) |
