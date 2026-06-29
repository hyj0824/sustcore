/**
 * @file vfile.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Virtual File Object
 * @version alpha-1.0.0
 * @date 2026-04-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <object/perm.h>
#include <object/vfile.h>
#include <vfs/vfs.h>

namespace cap {
    Result<size_t> VFileObject::read(off_t offset, void *buf, size_t len) {
        using namespace perm::vfile;
        if (!imply(READ)) {
            loggers::CAPABILITY::ERROR("权限不足");
            return {unexpect, ErrCode::INSUFFICIENT_PERMISSIONS};
        }
        // 调用VFS的read接口
        return VFS::inst().read(*_obj, offset, buf, len);
    }

    Result<size_t> VFileObject::write(off_t offset, const void *buf,
                                      size_t len) {
        using namespace perm::vfile;
        if (!imply(WRITE)) {
            loggers::CAPABILITY::ERROR("权限不足");
            return {unexpect, ErrCode::INSUFFICIENT_PERMISSIONS};
        }
        // 调用VFS的write接口
        return VFS::inst().write(*_obj, offset, buf, len);
    }

    Result<size_t> VFileObject::size() {
        using namespace perm::vfile;
        if (!imply(READ)) {
            loggers::CAPABILITY::ERROR("权限不足");
            return {unexpect, ErrCode::INSUFFICIENT_PERMISSIONS};
        }
        // 调用VFS的size接口
        return VFS::inst().size(*_obj);
    }

    Result<void> VFileObject::sync() {
        using namespace perm::vfile;
        if (!imply(WRITE)) {
            loggers::CAPABILITY::ERROR("权限不足");
            return {unexpect, ErrCode::INSUFFICIENT_PERMISSIONS};
        }
        // 调用VFS的sync接口
        return VFS::inst().sync(*_obj);
    }

    Result<void> VFileObject::ioctl(size_t cmd, syscall::UBuffer &&arg) {
        using namespace perm::vfile;
        if (!imply(READ)) {
            loggers::CAPABILITY::ERROR("权限不足");
            return {unexpect, ErrCode::INSUFFICIENT_PERMISSIONS};
        }
        return VFS::inst().ioctl(*_obj, cmd, std::move(arg));
    }
}  // namespace cap
