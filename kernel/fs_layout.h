#ifndef FS_LAYOUT_H
#define FS_LAYOUT_H

/*
 * fs_layout.h - FUNSOS 文件系统目录布局初始化
 *
 * 在内核启动时自动构建标准 Unix 风格目录结构。
 */

void fs_build_layout(void);

#endif /* FS_LAYOUT_H */
