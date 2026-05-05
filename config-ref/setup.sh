#!/bin/bash
#
# 仅支持某些发行版

set -e  # 遇到错误立即退出

# 可选：传入 --force 或 -f 强制重新执行所有步骤
FORCE=0
if [ "$1" = "--force" ] || [ "$1" = "-f" ]; then
    FORCE=1
fi

STATE_DIR="$PWD/.setup_state"
mkdir -p "$STATE_DIR"

echo "输入当前发行版的包管理器（仅输入序号）：
    1) apt
    2) pacman
    3) 其他"
read packman

if [ $FORCE -eq 1 ] && [ -d "$STATE_DIR" ]; then
    rm -rf "$STATE_DIR"
    mkdir -p "$STATE_DIR"
fi

if [ -f "$STATE_DIR/packages_installed" ] && [ $FORCE -eq 0 ]; then
    echo "已安装系统依赖，跳过此步骤"
else
    case $packman in
        1) sudo apt install   qemu-system-misc make gcc g++ ninja-build libsdl2-dev texinfo curl wget git m4
        ;;
        2) sudo pacman -S     qemu-system-misc make gcc g++ ninja-build libsdl2-dev texinfo curl wget git m4
        ;;
        *) echo "暂不支持该发行版，请手动安装gcc g++ ninja-build libsdl2-dev texinfo curl wget git m4（软件包名称可能因发行版而不同）"
        ;;
    esac
    touch "$STATE_DIR/packages_installed"
fi

echo -e "\\e[34mdownloading packages...\\e[0m"

export MPFR=mpfr-4.1.0
export MPC=mpc-1.2.1
export GMP=gmp-6.2.1
export BINUTILS=binutils-2.44
export GCCV=gcc-15.2.0

download_if_needed() {
    url=$1
    out=$2
    if [ -f "$out" ] && [ $FORCE -eq 0 ]; then
        echo "已存在 $out，跳过下载"
    else
        curl "$url" --output "$out"
    fi
}

if [ -f "$STATE_DIR/downloads_done" ] && [ $FORCE -eq 0 ]; then
    echo "已完成下载，跳过此步骤"
else
    download_if_needed "https://mirrors.aliyun.com/gnu/mpfr/$MPFR.tar.xz" ./$MPFR.tar.xz
    download_if_needed "https://mirrors.aliyun.com/gnu/mpc/$MPC.tar.gz" ./$MPC.tar.gz
    download_if_needed "https://mirrors.aliyun.com/gnu/gmp/$GMP.tar.xz" ./$GMP.tar.xz
    download_if_needed "https://mirrors.aliyun.com/gnu/binutils/$BINUTILS.tar.xz" ./$BINUTILS.tar.xz
    download_if_needed "https://mirrors.aliyun.com/gnu/gcc/$GCCV/$GCCV.tar.xz" ./$GCCV.tar.xz
    touch "$STATE_DIR/downloads_done"
fi

#解压
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

extract_once "mpfr_extracted" ./$MPFR.tar.xz "$MPFR"
extract_once "mpc_extracted"  ./$MPC.tar.gz  "$MPC"
extract_once "gmp_extracted"  ./$GMP.tar.xz  "$GMP"
extract_once "binutils_extracted" ./$BINUTILS.tar.xz "$BINUTILS"
extract_once "gcc_extracted" ./$GCCV.tar.xz "$GCCV"

export PREFIX="$HOME/opt/cross"
export TARGET1=x86_64-elf
export TARGET2=riscv64-unknown-elf
export TARGET3=loongarch64-unknown-elf
export PATH="$PREFIX/bin:$PATH"

# 创建必要的目录
mkdir -p "$PREFIX"

#编译GMP, MPFR与MPC

echo -e "\\e[34mbuilding gmp\\e[0m"
if [ -f "$STATE_DIR/gmp_built" ] && [ $FORCE -eq 0 ]; then
    echo "GMP 已构建并安装，跳过"
else
    cd ./$GMP/
        mkdir -p ./build/
        cd ./build/
            ../configure
            make -j2
            sudo make install
        cd ../
    cd ../
    touch "$STATE_DIR/gmp_built"
fi

echo -e "\\e[34mbuilding mpfr\\e[0m"
if [ -f "$STATE_DIR/mpfr_built" ] && [ $FORCE -eq 0 ]; then
    echo "MPFR 已构建并安装，跳过"
else
    cd ./$MPFR/
        mkdir -p ./build/
        cd ./build/
            ../configure
            make -j2
            sudo make install
        cd ../
    cd ../
    touch "$STATE_DIR/mpfr_built"
fi

echo -e "\\e[34mbuilding mpc\\e[0m"
if [ -f "$STATE_DIR/mpc_built" ] && [ $FORCE -eq 0 ]; then
    echo "MPC 已构建并安装，跳过"
else
    cd ./$MPC/
        mkdir -p ./build/
        cd ./build/
            ../configure
            make -j2
            sudo make install
        cd ../
    cd ../
    touch "$STATE_DIR/mpc_built"
fi

# 为X86-64编译GCC

echo -e "\\e[34mbuilding binutils for $TARGET1\\e[0m"
if [ -f "$STATE_DIR/binutils_$TARGET1" ] && [ $FORCE -eq 0 ]; then
    echo "binutils for $TARGET1 已构建并安装，跳过"
else
    cd ./$BINUTILS/
        BUILD_DIR=./build-$TARGET1
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
            make distclean || true
            ../configure --target=$TARGET1 --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
            make -j2
            make install
        cd ../
    cd ../
    touch "$STATE_DIR/binutils_$TARGET1"
fi

which -- $TARGET1-as || echo $TARGET1-as is not in the PATH

echo -e "\\e[34mbuilding gcc\\e[0m"
if [ -f "$STATE_DIR/gcc_$TARGET1" ] && [ $FORCE -eq 0 ]; then
    echo "gcc for $TARGET1 已构建并安装，跳过"
else
    cd ./$GCCV/
        BUILD_DIR=./build-$TARGET1
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
            make distclean || true
            ../configure --target=$TARGET1 --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
            make all-gcc -j2
            make all-target-libgcc -j2
            make install-gcc
            make install-target-libgcc
        cd ../
    cd ../
    touch "$STATE_DIR/gcc_$TARGET1"
fi

# 为RISCV编译GCC

echo -e "\\e[34mbuilding binutils for $TARGET2\\e[0m"
if [ -f "$STATE_DIR/binutils_$TARGET2" ] && [ $FORCE -eq 0 ]; then
    echo "binutils for $TARGET2 已构建并安装，跳过"
else
    cd ./$BINUTILS/
        BUILD_DIR=./build-$TARGET2
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
            make distclean || true
            ../configure --target=$TARGET2 --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
            make -j8
            make install
        cd ../
    cd ../
    touch "$STATE_DIR/binutils_$TARGET2"
fi

which -- $TARGET2-as || echo $TARGET2-as is not in the PATH

echo -e "\\e[34mbuilding gcc\\e[0m"
if [ -f "$STATE_DIR/gcc_$TARGET2" ] && [ $FORCE -eq 0 ]; then
    echo "gcc for $TARGET2 已构建并安装，跳过"
else
    cd ./$GCCV/
        BUILD_DIR=./build-$TARGET2
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
            make distclean || true
            ../configure --target=$TARGET2 --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
            make all-gcc -j2
            make all-target-libgcc -j2
            make install-gcc
            make install-target-libgcc
        cd ../
    cd ../
    touch "$STATE_DIR/gcc_$TARGET2"
fi

# 为LOONGARCH编译GCC

echo -e "\\e[34mbuilding binutils for $TARGET3\\e[0m"
if [ -f "$STATE_DIR/binutils_$TARGET3" ] && [ $FORCE -eq 0 ]; then
    echo "binutils for $TARGET3 已构建并安装，跳过"
else
    cd ./$BINUTILS/
        BUILD_DIR=./build-$TARGET3
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
            make distclean || true
            ../configure --target=$TARGET3 --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
            make -j2
            make install
        cd ../
    cd ../
    touch "$STATE_DIR/binutils_$TARGET3"
fi

which -- $TARGET3-as || echo $TARGET3-as is not in the PATH

echo -e "\\e[34mbuilding gcc\\e[0m"
if [ -f "$STATE_DIR/gcc_$TARGET3" ] && [ $FORCE -eq 0 ]; then
    echo "gcc for $TARGET3 已构建并安装，跳过"
else
    cd ./$GCCV/
        BUILD_DIR=./build-$TARGET3
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
            make distclean || true
            ../configure --target=$TARGET3 --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
            make all-gcc -j2
            make all-target-libgcc -j2
            make install-gcc
            make install-target-libgcc
        cd ../
    cd ../
    touch "$STATE_DIR/gcc_$TARGET3"
fi

echo "全部步骤完成。若需要强制重跑，请使用 ./setup.sh --force"
