/**
 * @file ls7a.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief LS7A RTC 设备驱动
 * @version alpha-1.0.0
 * @date 2026-06-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/model.h>
#include <driver/rtc/ls7a.h>
#include <logger.h>
#include <sustcore/errcode.h>
#include <vfs/vfs.h>

namespace driver {
    namespace {
        constexpr FDTDeviceId LS7A_RTC_FDT_IDS[] = {
            {.compatible = "loongson,ls7a-rtc", .driver_flag = 0},
            {.compatible = nullptr, .driver_flag = 0},
        };
        constexpr DeviceId LS7A_RTC_DEVICE_ID = {
            .fdt_ids = LS7A_RTC_FDT_IDS,
            .pci_ids = nullptr,
        };

        class LS7ARtcFile final : public devfs::CharDevFile {
        private:
            LS7ARTC &_device;

        public:
            LS7ARtcFile(inode_t inode_id, LS7ARTC &device)
                : CharDevFile(inode_id), _device(device) {}

            [[nodiscard]]
            Result<size_t> write(const void *buf, size_t len) override {
                (void)buf;
                (void)len;
                unexpect_return(ErrCode::NOT_SUPPORTED);
            }

            [[nodiscard]]
            Result<void> ioctl(size_t cmd, syscall::UBuffer &&arg) override {
                return _device.ioctl(cmd, std::move(arg));
            }
        };

        Result<util::owner<IINode *>> create_ls7a_rtc_file(void *ctx,
                                                            inode_t inode_id) {
            auto *device = static_cast<LS7ARTC *>(ctx);
            auto *file   = new LS7ARtcFile(inode_id, *device);
            if (file == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            return util::owner<IINode *>(file);
        }

        struct ToyTime {
            sus_u32 sec;
            sus_u32 min;
            sus_u32 hour;
            sus_u32 day;
            sus_u32 month;
            sus_u32 year;
        };

        constexpr sus_u32 TOY_MON_SHIFT  = 26;
        constexpr sus_u32 TOY_MON_MASK   = 0x3F;
        constexpr sus_u32 TOY_DAY_SHIFT  = 21;
        constexpr sus_u32 TOY_DAY_MASK   = 0x1F;
        constexpr sus_u32 TOY_HOUR_SHIFT = 16;
        constexpr sus_u32 TOY_HOUR_MASK  = 0x1F;
        constexpr sus_u32 TOY_MIN_SHIFT  = 10;
        constexpr sus_u32 TOY_MIN_MASK   = 0x3F;
        constexpr sus_u32 TOY_SEC_SHIFT  = 4;
        constexpr sus_u32 TOY_SEC_MASK   = 0x3F;

        [[nodiscard]]
        constexpr bool is_leap_year(int64_t year) noexcept {
            return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        }

        [[nodiscard]]
        constexpr int64_t days_before_month(int64_t year, int64_t month) noexcept {
            static constexpr int DAYS_BEFORE_MONTH[] = {
                0,   31,  59,  90,  120, 151,
                181, 212, 243, 273, 304, 334,
            };
            if (month <= 1) {
                return 0;
            }
            int64_t days = DAYS_BEFORE_MONTH[month - 1];
            if (month > 2 && is_leap_year(year)) {
                days += 1;
            }
            return days;
        }

        [[nodiscard]]
        constexpr int64_t days_from_civil(int64_t year, int64_t month,
                                          int64_t day) noexcept {
            year -= month <= 2 ? 1 : 0;
            const int64_t era = (year >= 0 ? year : year - 399) / 400;
            const int64_t yoe = year - era * 400;
            const int64_t doy =
                days_before_month(year + (month <= 2 ? 1 : 0), month) + day - 1;
            const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }

        [[nodiscard]]
        constexpr sus_u32 decode_sec(sus_u32 toy_read0) noexcept {
            return (toy_read0 >> TOY_SEC_SHIFT) & TOY_SEC_MASK;
        }

        [[nodiscard]]
        constexpr sus_u32 decode_min(sus_u32 toy_read0) noexcept {
            return (toy_read0 >> TOY_MIN_SHIFT) & TOY_MIN_MASK;
        }

        [[nodiscard]]
        constexpr sus_u32 decode_hour(sus_u32 toy_read0) noexcept {
            return (toy_read0 >> TOY_HOUR_SHIFT) & TOY_HOUR_MASK;
        }

        [[nodiscard]]
        constexpr sus_u32 decode_day(sus_u32 toy_read0) noexcept {
            return (toy_read0 >> TOY_DAY_SHIFT) & TOY_DAY_MASK;
        }

        [[nodiscard]]
        constexpr sus_u32 decode_month(sus_u32 toy_read0) noexcept {
            return (toy_read0 >> TOY_MON_SHIFT) & TOY_MON_MASK;
        }

        [[nodiscard]]
        constexpr sus_u32 decode_year(sus_u32 toy_read1) noexcept {
            return 1900 + toy_read1;
        }

        [[nodiscard]]
        ToyTime decode_toy_time(sus_u32 toy_read0, sus_u32 toy_read1) noexcept {
            return ToyTime{
                .sec   = decode_sec(toy_read0),
                .min   = decode_min(toy_read0),
                .hour  = decode_hour(toy_read0),
                .day   = decode_day(toy_read0),
                .month = decode_month(toy_read0),
                .year  = decode_year(toy_read1),
            };
        }

        [[nodiscard]]
        bool toy_time_valid(const ToyTime &time) noexcept {
            if (time.year < 1970 || time.month == 0 || time.month > 12 ||
                time.day == 0 || time.day > 31 || time.hour > 23 ||
                time.min > 59 || time.sec > 59) {
                return false;
            }
            return true;
        }

        [[nodiscard]]
        units::time toy_time_to_units(const ToyTime &time) noexcept {
            const int64_t days =
                days_from_civil(time.year, time.month, time.day);
            const int64_t seconds =
                days * 24 * 3600 + static_cast<int64_t>(time.hour) * 3600 +
                static_cast<int64_t>(time.min) * 60 + time.sec;
            return units::time::from_seconds(seconds);
        }
    }  // namespace

    LS7ARTC::LS7ARTC(DevRes res, char *base) noexcept
        : DriverBase(std::move(res)),
          regs(reinterpret_cast<volatile ReadRegs *>(base)) {
        assert(_mmios.size() >= 1);
        auto *mmio = _mmios[0].get();
        assert(mmio != nullptr);
        assert(mmio->region().size() >= RTC_REG_SIZE);

        auto *platform = device::DeviceModel::inst().platform();
        if (platform != nullptr) {
            if (platform->rpc() == nullptr) {
                platform->set_rpc(this);
            } else {
                loggers::DEVICE::WARN(
                    "LS7A RTC 未绑定到 Platform::rpc: 已存在 realtime provider");
            }
        }
    }

    LS7ARTC::~LS7ARTC() {
        auto *platform = device::DeviceModel::inst().platform();
        if (platform != nullptr) {
            platform->clear_rpc(this);
        }
    }

    units::time LS7ARTC::read_time() noexcept {
        sus_u32 read0_a = 0;
        sus_u32 read1   = 0;
        sus_u32 read0_b = 0;

        do {
            read0_a = regs->toy_read0;
            read1   = regs->toy_read1;
            read0_b = regs->toy_read0;
        } while (read0_a != read0_b);

        const auto decoded = decode_toy_time(read0_b, read1);
        if (!toy_time_valid(decoded)) {
            loggers::SUSTCORE::ERROR(
                "LS7A RTC 读取到非法时间: year=%u month=%u day=%u hour=%u min=%u sec=%u",
                decoded.year, decoded.month, decoded.day, decoded.hour,
                decoded.min, decoded.sec);
            return units::time{};
        }
        return toy_time_to_units(decoded);
    }

    void LS7ARTC::set_alarm(units::time when, AlarmHandler handler) noexcept {
        (void)when;
        (void)handler;
        loggers::SUSTCORE::DEBUG("LS7A RTC 当前未实现 alarm 编程");
    }

    Result<void> LS7ARTC::mount(CapIdx devdir) noexcept {
        auto &vfs = VFS::inst();
        auto devfs_res = vfs.devfs();
        propagate(devfs_res);
        auto &devfs = *devfs_res.value();

        auto lookup_res = holder().lookup(devdir);
        propagate(lookup_res);
        auto &dircap = *lookup_res.value();

        auto mkres = vfs.mkfile(dircap, "rtc", flags::O_READ, holder());
        propagate(mkres);

        lookup_res = holder().lookup(mkres.value());
        propagate(lookup_res);
        auto &filecap = *lookup_res.value();
        auto link_res = devfs.link_char(
            filecap, devfs::CharFactory{.ctx = this, .create = create_ls7a_rtc_file});
        propagate(link_res);
        void_return();
    }

    Result<void> LS7ARTC::ioctl(size_t cmd, syscall::UBuffer &&arg) noexcept {
        if (cmd != RTC_RD_TIME) {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
        if (arg.kbuf() == nullptr || arg.len() < sizeof(rtc_tm)) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        rtc_tm tm{};
        auto ft = read_time().to_formatted_time();
        tm.tm_sec   = static_cast<int>(ft.second);
        tm.tm_min   = static_cast<int>(ft.minute);
        tm.tm_hour  = static_cast<int>(ft.hour);
        tm.tm_mday  = static_cast<int>(ft.day);
        tm.tm_mon   = static_cast<int>(ft.month - 1);
        tm.tm_year  = static_cast<int>(ft.year - 1900);
        tm.tm_wday  = 0;
        tm.tm_yday  = 0;
        tm.tm_isdst = 0;

        memcpy(arg.kbuf(), &tm, sizeof(tm));
        return arg.commit_to_user(sizeof(tm));
    }

    const DeviceId &LS7ARTCFactory::device_id() const noexcept {
        return LS7A_RTC_DEVICE_ID;
    }

    Result<DriverBase *> LS7ARTCFactory::create(
        const device::DeviceNode &node, device::DeviceModel &model,
        b64 driver_flag) const {
        (void)model;
        (void)driver_flag;
        loggers::DEVICE::INFO("开始创建设备驱动: ls7a-rtc name=%s", node.name());

        auto virqs = device::DevResManager::get_virq_resource(node);
        auto mmios = device::DevResManager::get_mmio_resource(node);
        if (mmios.empty()) {
            loggers::DEVICE::ERROR("LS7A RTC 设备缺少 MMIO 资源!");
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        auto *mmio = mmios[0].get();
        assert(mmio != nullptr);
        assert(mmio->region().size() >= LS7ARTC::RTC_REG_SIZE);

        auto map_res = device::MMIOManager::inst().map_to_kernel(*mmio);
        if (!map_res.has_value()) {
            loggers::DEVICE::ERROR("LS7A RTC MMIO 资源映射失败!");
            unexpect_return(map_res.error());
        }

        return new LS7ARTC(
            DriverBase::DevRes(node, std::move(virqs), std::move(mmios)),
            map_res.value().as<char>());
    }
}  // namespace driver
