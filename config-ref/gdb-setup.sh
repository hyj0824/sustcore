#!/bin/bash
#
# 仅支持某些发行版

set -e  # 遇到错误立即退出

# 可选：传入 --force 或 -f 强制重新执行所有步骤
FORCE=0
if [ "$1" = "--force" ] || [ "$1" = "-f" ]; then
	FORCE=1
fi

STATE_DIR="$PWD/.gdb_setup_state"
mkdir -p "$STATE_DIR"

echo -e "\\e[34mdownloading packages...\\e[0m"

export GDB=gdb-16.3

download_if_needed() {
	url=$1
	out=$2
	if [ -f "$out" ] && [ $FORCE -eq 0 ]; then
		echo "已存在 $out，跳过下载"
	else
		curl -L "$url" --output "$out"
	fi
}

if [ -f "$STATE_DIR/downloads_done" ] && [ $FORCE -eq 0 ]; then
	echo "已完成下载，跳过此步骤"
else
	download_if_needed "https://mirrors.aliyun.com/gnu/gdb/$GDB.tar.xz" ./$GDB.tar.xz
	touch "$STATE_DIR/downloads_done"
fi

# 解压
extract_once() {
	marker=$1
	file=$2
	dir=$3
	if [ -f "$STATE_DIR/$marker" ] && [ $FORCE -eq 0 ]; then
		echo "已解压 $file，跳过"
	else
		echo "extracting archives for $dir"
		tar xf "$file"
		touch "$STATE_DIR/$marker"
	fi
}

extract_once "gdb_extracted" ./$GDB.tar.xz "$GDB"

export PREFIX="$HOME/opt/cross"
export TARGET1=x86_64-elf
export TARGET2=riscv64-unknown-elf
export TARGET3=loongarch64-unknown-elf
export PATH="$PREFIX/bin:$PATH"

# 创建必要的目录
mkdir -p "$PREFIX"

build_gdb_for() {
	target="$1"
	marker="gdb_$target"
	echo -e "\\e[34mbuilding gdb for $target\\e[0m"
	if [ -f "$STATE_DIR/$marker" ] && [ $FORCE -eq 0 ]; then
		echo "gdb for $target 已构建并安装，跳过"
		return
	fi

	cd ./$GDB/
		BUILD_DIR=./build-$target
		mkdir -p "$BUILD_DIR"
		cd "$BUILD_DIR"
			if [ $FORCE -eq 1 ]; then
				make distclean || true
				rm -f config.cache || true
			fi
			make distclean || true
			../configure --target=$target --prefix="$PREFIX" --disable-werror
			make all-gdb -j2
			make install-gdb
		cd ../
	cd ../
	touch "$STATE_DIR/$marker"
}

# 为x86-64编译GDB
build_gdb_for "$TARGET1"

# 为riscv编译GDB
build_gdb_for "$TARGET2"

# 为LOONGARCH编译GDB
build_gdb_for "$TARGET3"

echo "GDB 全部步骤完成。若需要强制重跑，请使用 ./gdb-setup.sh --force 或 ./gdb-setup.sh -f"
