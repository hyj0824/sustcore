/**
 * @file fdt.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief
 * @version alpha-1.0.0
 * @date 2026-05-22
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/model.h>
#include <logger.h>
#include <sus/rtti.h>
#include <sus/types.h>
#include <sustcore/addr.h>
#include <sustcore/errcode.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fdt {
    namespace detail {
        [[nodiscard]]
        constexpr bool parse_be_integer(const char *data, size_t size,
                                        uint64_t &value) {
            if (data == nullptr || size == 0 || size > sizeof(uint64_t)) {
                return false;
            }

            value = 0;
            for (size_t i = 0; i < size; ++i) {
                value = (value << 8) | static_cast<unsigned char>(data[i]);
            }
            return true;
        }
    }  // namespace detail

    using phandle_t = b32;

    constexpr const char *COMPATIBLE_PROP  = "compatible";
    constexpr const char *DEVICE_TYPE_PROP = "device_type";
    constexpr const char *STATUS_PROP      = "status";
    constexpr const char *OKAY_STATUS      = "okay";
    constexpr const char *OK_STATUS        = "ok";
    constexpr const char *DISABLED_STATUS  = "disabled";

    struct RegionCells {
        size_t addr_cells;
        size_t size_cells;
    };

    struct Property {
        std::string name;
        const char *data;  // 直接指向 dtb 中的数据
        size_t size;       // 数据大小 (字节)

        [[nodiscard]]
        constexpr std::vector<std::string> as_string_list() const {
            std::vector<std::string> result;
            std::string str = "";
            for (int i = 0; i < size; i++) {
                if (data[i] == '\0') {
                    if (!str.empty()) {
                        result.push_back(str);
                        str.clear();
                    }
                } else {
                    str += data[i];
                }
            }
            if (!str.empty()) {
                result.push_back(str);
            }
            return result;
        }

        /**
         * @brief 将属性解析为设备树 reg 风格的地址区间列表.
         *
         * 属性数据会按大端 cell 序列解释, 每个 cell 固定为 4 字节.
         * 每个区间由 `addr_cells` 个地址 cell 与 `size_cells` 个长度 cell
         * 组成, 返回值中的每个 `PhyArea` 均满足 `[begin, begin + size)`.
         *
         * 若属性长度非法、cell 数为 0、或单个地址/长度超出 64 位表示范围,
         * 则在调试构建中触发 `assert`, 并返回空列表作为哨兵值.
         *
         * @param cells 地址与长度字段各自占用的 cell 数量.
         * @return std::vector<PhyArea> 解析得到的区间列表.
         */
        [[nodiscard]]
        constexpr std::vector<PhyArea> as_regions(
            const RegionCells &cells) const {
            constexpr size_t CELL_SIZE = sizeof(sus_u32);

            if (data == nullptr || cells.addr_cells == 0 ||
                cells.size_cells == 0)
            {
                assert(false && "invalid property buffer or region cells");
                return {};
            }

            size_t total_cells      = cells.addr_cells + cells.size_cells;
            size_t bytes_per_region = total_cells * CELL_SIZE;
            size_t addr_size        = cells.addr_cells * CELL_SIZE;
            size_t size_size        = cells.size_cells * CELL_SIZE;

            if (bytes_per_region == 0 || size % bytes_per_region != 0) {
                assert(false && "invalid property size for reg regions");
                return {};
            }
            if (addr_size > sizeof(uint64_t) || size_size > sizeof(uint64_t)) {
                assert(false && "reg cell width exceeds uint64_t");
                return {};
            }

            std::vector<PhyArea> result;
            result.reserve(size / bytes_per_region);
            for (size_t offset = 0; offset < size; offset += bytes_per_region) {
                uint64_t begin_raw = 0;
                uint64_t size_raw  = 0;
                bool ok = detail::parse_be_integer(data + offset, addr_size,
                                                   begin_raw) &&
                          detail::parse_be_integer(data + offset + addr_size,
                                                   size_size, size_raw);
                if (!ok ||
                    size_raw > static_cast<uint64_t>(MAX_ADDR) - begin_raw)
                {
                    assert(false && "invalid reg region value");
                    return {};
                }

                result.push_back(PhyArea(
                    PhyAddr(static_cast<addr_t>(begin_raw)),
                    PhyAddr(static_cast<addr_t>(begin_raw + size_raw))));
            }
            return result;
        }

        /**
         * @brief 将属性解析为大端无符号整数.
         *
         * 属性数据按 DTB 的大端编码解释, 支持 1 到 8 字节的整数字段.
         * 若属性为空、数据指针为空、或长度超过 64 位表示范围, 则在调试构建
         * 中触发 `assert`, 并返回 `0` 作为哨兵值.
         *
         * @return uint64_t 解析出的无符号整数值.
         */
        [[nodiscard]]
        constexpr uint64_t as_integral() const {
            uint64_t value = 0;
            if (!detail::parse_be_integer(data, size, value)) {
                assert(false && "invalid property integral width");
                return 0;
            }
            return value;
        }
        [[nodiscard]]
        constexpr phandle_t as_phandle() const {
            return as_integral();
        }
        [[nodiscard]]
        constexpr std::string as_string() const {
            return as_string_list().empty() ? "" : as_string_list()[0];
        }
        [[nodiscard]]
        constexpr std::vector<byte> as_byte_array() const {
            return {data, data + size};
        }
    };

    struct Node {
        std::string name;
        std::unordered_map<std::string, util::owner<Property *>> properties;
        std::unordered_map<std::string, util::owner<Node *>> children;
        Node *parent      = nullptr;
        phandle_t phandle = 0;

        Node() = default;

        Node(const Node &other)
            : name(other.name),
              properties(),
              children(),
              parent(other.parent),
              phandle(other.phandle) {
            for (const auto &[key, prop] : other.properties) {
                properties[key] = util::owner(new Property(*prop));
            }
            for (const auto &[key, child] : other.children) {
                children[key] = util::owner(new Node(*child));
            }
        }
        Node &operator=(const Node &other) {
            if (this != &other) {
                cleanup();

                name    = other.name;
                parent  = other.parent;
                phandle = other.phandle;
                for (const auto &[key, prop] : other.properties) {
                    properties[key] = util::owner(new Property(*prop));
                }
                for (const auto &[key, child] : other.children) {
                    children[key] = util::owner(new Node(*child));
                }
            }
            return *this;
        }
        Node(Node &&other)
            : name(std::move(other.name)),
              properties(std::move(other.properties)),
              children(std::move(other.children)),
              parent(other.parent),
              phandle(other.phandle) {}
        Node &operator=(Node &&other) {
            if (this != &other) {
                cleanup();
                name       = std::move(other.name);
                properties = std::move(other.properties);
                children   = std::move(other.children);
                parent     = other.parent;
                phandle    = other.phandle;
            }
            return *this;
        }

        void cleanup() {
            for (auto &[_, prop] : properties) {
                delete prop;
            }
            for (auto &[_, child] : children) {
                delete child;
            }
            properties.clear();
            children.clear();
        }

        ~Node() {
            cleanup();
        }
    };

    struct Configuration {
        util::owner<Node *> root;
        std::unordered_map<phandle_t, Node *> phandle_map;

        [[nodiscard]]
        Node *get_node_by_path(const std::string &path) const;
        [[nodiscard]]
        Node *get_node_by_phandle(phandle_t phandle) const;
        [[nodiscard]]
        Result<void> add_node(Node *parent, util::owner<Node *> new_node);
        [[nodiscard]]
        Result<void> remove_node(Node *node);
    };

    void make_config(void *dtb, Configuration &config);

    class FDTProvider : public device::DeviceProvider {
    private:
        Configuration _config;

        void append_as_regions(std::vector<device::MemRegion> &regions,
                               const RegionCells &cells, const Property &prop,
                               device::MemRegion::MemoryStatus status) const;

        bool is_memory_node(Node &node) const;

    public:
        FDTProvider(void *dtb) {
            make_config(dtb, _config);
        }
        void collect_memory_regions(
            std::vector<device::MemRegion> &regions) const override;
        void update_cpus(device::CpuGroupInfo &cpus) const override;
        [[nodiscard]]
        const char *name() const override {
            return "fdt";
        }
    };
}  // namespace fdt