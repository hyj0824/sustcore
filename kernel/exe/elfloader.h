/**
 * @file elfloader.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief ELF文件加载器
 * @version alpha-1.0.0
 * @date 2026-04-17
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <exe/task.h>

namespace loader::elf {
	// 解析 ELF 并将 PT_LOAD 段加载到 TaskSpec 的地址空间中. 
	// 该实现不直接映射页, 依赖缺页异常路径完成物理页分配与映射. 
	Result<void> load(TaskSpec &spec, const LoadPrm &prm);
	Result<void> load_segments(TaskSpec &spec, const LoadPrm &prm,
	                           bool create_heap);
};
