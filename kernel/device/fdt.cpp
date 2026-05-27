/**
 * @file fdt.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief FDT 设备提供器实现
 * @version alpha-1.0.0
 * @date 2026-05-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <arch/riscv64/device/fdt_helper.h>
#include <device/fdt.h>
#include <device/model.h>
#include <libfdt.h>
#include <logger.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace {
    constexpr const char *PHANDLE_PROP       = "phandle";
    constexpr const char *LINUX_PHANDLE_PROP = "linux,phandle";
    constexpr const char *REG_PROP           = "reg";
    constexpr const char *ADDRESS_CELLS_PROP = "#address-cells";
    constexpr const char *SIZE_CELLS_PROP    = "#size-cells";
    constexpr const char *NO_MAP_PROP        = "no-map";
    constexpr const char *STATUS_PROP        = "status";
    constexpr const char *DEVICE_TYPE_PROP   = "device_type";
    constexpr const char *TIMEBASE_FREQ_PROP = "timebase-frequency";
    constexpr const char *MODEL_PROP         = "model";
    constexpr const char *MMU_TYPE_PROP      = "mmu-type";
    constexpr const char *RISCV_ISA_PROP     = "riscv,isa";
    constexpr const char *CPU_PROP           = "cpu";
    constexpr const char *INTERRUPT_EXT_PROP = "interrupts-extended";
    constexpr const char *INTC_PROP          = "interrupt-controller";
    constexpr const char *CPU_MAP_NODE       = "cpu-map";

    constexpr const char *OKAY_STATUS        = "okay";
    constexpr const char *MEMORY_DEVICE_TYPE = "memory";
    constexpr const char *CPU_DEVICE_TYPE    = "cpu";
    constexpr const char *CLINT_COMPATIBLE   = "riscv,clint0";

    constexpr const char *RESERVED_MEMORY_PATH = "/reserved-memory";
    constexpr const char *CPUS_PATH            = "/cpus";

    struct ParsedCpu {
        device::cpuid_t id;
        std::string model;
        std::string isa_string;
        std::string mmu_type;
        fdt::phandle_t cpu_phandle;
        fdt::phandle_t local_intc_phandle;
    };

    struct ClintDescriptor {
        bool found                 = false;
        device::ictrl_t identifier = device::INVALID_ICTRL_ID;
        std::string name;
        std::vector<PhyArea> mmio_regions;
        std::vector<device::cpuid_t> target_harts;
    };

    /**
     * @brief 判断节点状态是否为启用.
     */
    [[nodiscard]]
    bool node_status_enabled(const fdt::Node &node) {
        auto it = node.properties.find(STATUS_PROP);
        if (it == node.properties.end()) {
            return true;
        }

        std::string status = it->second->as_string();
        return status == fdt::OKAY_STATUS || status == fdt::OK_STATUS;
    }

    /**
     * @brief 获取节点 reg 属性的地址/长度 cell 配置.
     */
    [[nodiscard]]
    fdt::RegionCells node_region_cells(const fdt::Node &node) {
        size_t addr_cells = 2;
        size_t size_cells = 2;

        if (node.parent != nullptr) {
            auto addr_it = node.parent->properties.find(ADDRESS_CELLS_PROP);
            if (addr_it != node.parent->properties.end()) {
                addr_cells =
                    static_cast<size_t>(addr_it->second->as_integral());
            }

            auto size_it = node.parent->properties.find(SIZE_CELLS_PROP);
            if (size_it != node.parent->properties.end()) {
                size_cells =
                    static_cast<size_t>(size_it->second->as_integral());
            }
        }

        return {.addr_cells = addr_cells, .size_cells = size_cells};
    }

    /**
     * @brief 递归构建设备树节点对象.
     */
    void build_node_recursive(const void *dtb, fdt::Configuration &config,
                              fdt::Node &node, int node_offset) {
        int prop_offset;
        fdt_for_each_property_offset(prop_offset, dtb, node_offset) {
            const char *prop_name = nullptr;
            int prop_len          = 0;
            const void *prop_data =
                fdt_getprop_by_offset(dtb, prop_offset, &prop_name, &prop_len);
            if (prop_name == nullptr || prop_data == nullptr || prop_len < 0) {
                continue;
            }

            auto *prop = new fdt::Property{
                .name = prop_name,
                .data = static_cast<const char *>(prop_data),
                .size = static_cast<size_t>(prop_len),
            };
            node.properties[prop->name] = util::owner(prop);

            if ((prop->name == PHANDLE_PROP ||
                 prop->name == LINUX_PHANDLE_PROP) &&
                node.phandle == 0)
            {
                node.phandle = prop->as_phandle();
            }
        }

        if (node.phandle != 0) {
            config.phandle_map[node.phandle] = &node;
        }

        int child_offset;
        fdt_for_each_subnode(child_offset, dtb, node_offset) {
            const char *child_name = fdt_get_name(dtb, child_offset, nullptr);
            if (child_name == nullptr) {
                continue;
            }

            auto child    = util::owner(new fdt::Node());
            child->name   = child_name;
            child->parent = &node;

            build_node_recursive(dtb, config, *child, child_offset);
            node.children[child->name] = std::move(child);
        }
    }

    /**
     * @brief 判断节点是否存在指定属性.
     */
    [[nodiscard]]
    bool has_property(const fdt::Node &node, const char *prop_name) {
        return node.properties.contains(prop_name);
    }

    /**
     * @brief 判断字符串属性是否等于给定值.
     */
    [[nodiscard]]
    bool is_string_prop_equal(const fdt::Node &node, const char *prop_name,
                              const char *expected) {
        auto it = node.properties.find(prop_name);
        if (it == node.properties.end()) {
            return false;
        }
        return it->second->as_string() == expected;
    }

    /**
     * @brief 判断 compatible 列表中是否包含目标兼容串.
     */
    [[nodiscard]]
    bool compatible_contains(const fdt::Node &node,
                             const char *expected) noexcept {
        auto it = node.properties.find(fdt::COMPATIBLE_PROP);
        if (it == node.properties.end()) {
            return false;
        }

        for (const auto &compatible : it->second->as_string_list()) {
            if (compatible == expected) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 解析节点的 reg 单整数值.
     */
    [[nodiscard]]
    std::optional<uint64_t> parse_reg_value(const fdt::Node &node) noexcept {
        auto reg_it = node.properties.find(REG_PROP);
        if (reg_it == node.properties.end()) {
            return std::nullopt;
        }
        return reg_it->second->as_integral();
    }

    /**
     * @brief 按名称排序返回子节点列表.
     */
    [[nodiscard]]
    std::vector<const fdt::Node *> sorted_children(const fdt::Node &node) {
        std::vector<const fdt::Node *> result;
        result.reserve(node.children.size());
        for (const auto &[_, child] : node.children) {
            result.push_back(child.get());
        }
        std::ranges::sort(result,
                          [](const fdt::Node *lhs, const fdt::Node *rhs) {
                              return lhs->name < rhs->name;
                          });
        return result;
    }

    /**
     * @brief 为 CPU 节点推导可读型号字符串.
     */
    [[nodiscard]]
    std::string fallback_cpu_model(const fdt::Node &node) {
        auto model_it = node.properties.find(MODEL_PROP);
        if (model_it != node.properties.end()) {
            std::string model = model_it->second->as_string();
            if (!model.empty()) {
                return model;
            }
        }

        auto compatible_it = node.properties.find(fdt::COMPATIBLE_PROP);
        if (compatible_it != node.properties.end()) {
            auto compatibles = compatible_it->second->as_string_list();
            if (!compatibles.empty()) {
                return compatibles.front();
            }
        }

        return node.name;
    }

    /**
     * @brief 提取 CPU 节点下本地中断控制器的 phandle.
     */
    [[nodiscard]]
    std::optional<fdt::phandle_t> find_local_intc_phandle(
        const fdt::Node &cpu_node) noexcept {
        for (const auto &[_, child] : cpu_node.children) {
            if (!has_property(*child, INTC_PROP)) {
                continue;
            }
            if (child->phandle != 0) {
                return child->phandle;
            }
            loggers::DEVICE::WARN(
                "CPU 节点 %s 的 interrupt-controller 缺少 phandle",
                cpu_node.name.c_str());
            return std::nullopt;
        }
        return std::nullopt;
    }

    /**
     * @brief 将属性数据解析为 u32 cell 数组.
     */
    [[nodiscard]]
    std::vector<uint32_t> parse_u32_cells(const fdt::Property &prop) {
        constexpr size_t CELL_SIZE = sizeof(uint32_t);
        if (prop.size % CELL_SIZE != 0) {
            return {};
        }

        std::vector<uint32_t> cells;
        cells.reserve(prop.size / CELL_SIZE);
        for (size_t offset = 0; offset < prop.size; offset += CELL_SIZE) {
            uint64_t value = 0;
            if (!fdt::detail::parse_be_integer(prop.data + offset, CELL_SIZE,
                                               value))
            {
                return {};
            }
            cells.push_back(static_cast<uint32_t>(value));
        }
        return cells;
    }

    /**
     * @brief 将 cpu-map 节点名映射为拓扑层级.
     */
    [[nodiscard]]
    std::optional<device::CpuTopoLevel> topo_level_from_name(
        std::string_view name) noexcept {
        if (name.starts_with("thread")) {
            return device::CpuTopoLevel::THREAD;
        }
        if (name.starts_with("core")) {
            return device::CpuTopoLevel::CORE;
        }
        if (name.starts_with("cluster")) {
            return device::CpuTopoLevel::CLUSTER;
        }
        if (name.starts_with("package") || name.starts_with("socket")) {
            return device::CpuTopoLevel::PACKAGE;
        }
        if (name.starts_with("numa")) {
            return device::CpuTopoLevel::NUMA;
        }
        return std::nullopt;
    }

    /**
     * @brief 递归构建 cpu-map 子树.
     */
    [[nodiscard]]
    Result<std::vector<device::cpuid_t>> build_cpu_map_subtree(
        device::CpuTopologyBuilder &builder, const fdt::Node &map_node,
        device::topo_t parent_id, device::topo_t &next_topo_id,
        const std::unordered_map<fdt::phandle_t, device::cpuid_t>
            &cpu_phandle_map) {
        // 识别当前拓扑层级.
        auto level = topo_level_from_name(map_node.name);
        if (!level.has_value()) {
            loggers::DEVICE::WARN("忽略未知 cpu-map 拓扑节点: %s",
                                  map_node.name.c_str());
            return std::vector<device::cpuid_t>{};
        }

        device::topo_t node_id = next_topo_id++;
        auto add_res           = builder.add_child(parent_id, *level, node_id);
        if (!add_res.has_value()) {
            propagate_return(add_res);
        }

        // 收集当前节点直接声明的 CPU.
        std::vector<device::cpuid_t> aggregated;
        auto cpu_it = map_node.properties.find(CPU_PROP);
        if (cpu_it != map_node.properties.end()) {
            fdt::phandle_t cpu_phandle = cpu_it->second->as_phandle();
            auto cpu_map_it            = cpu_phandle_map.find(cpu_phandle);
            if (cpu_map_it == cpu_phandle_map.end()) {
                loggers::DEVICE::ERROR(
                    "cpu-map 节点 %s 引用了未知 CPU phandle=%u",
                    map_node.name.c_str(), cpu_phandle);
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            aggregated.push_back(cpu_map_it->second);
        }

        // 递归构建子节点.
        for (const auto *child : sorted_children(map_node)) {
            auto child_res = build_cpu_map_subtree(
                builder, *child, node_id, next_topo_id, cpu_phandle_map);
            propagate(child_res);
            const auto &child_cpus = child_res.value();
            aggregated.insert(aggregated.end(), child_cpus.begin(),
                              child_cpus.end());
        }

        // 规范化 CPU 列表并回填到节点.
        std::ranges::sort(aggregated);
        aggregated.erase(
            std::ranges::unique(aggregated,
                                [](device::cpuid_t lhs, device::cpuid_t rhs) {
                                    return lhs == rhs;
                                }),
            aggregated.end());

        auto cpus_res = builder.cpus(node_id, aggregated);
        propagate(cpus_res);
        return aggregated;
    }

    /**
     * @brief 构造保底 CPU 拓扑.
     */
    [[nodiscard]]
    Result<device::CpuTopology> build_default_topology(
        const std::vector<device::cpuid_t> &cpu_ids) {
        device::CpuTopologyBuilder builder;

        // 构造根 cluster.
        builder.root(device::CpuTopoLevel::CLUSTER, 0);

        auto root_res = builder.cpus(0, cpu_ids);
        propagate(root_res);

        // 为每个 CPU 创建独立 core 节点.
        device::topo_t next_id = 1;
        for (device::cpuid_t cpu_id : cpu_ids) {
            auto add_res =
                builder.add_child(0, device::CpuTopoLevel::CORE, next_id);
            propagate(add_res);
            auto cpu_res = builder.cpus(next_id, {cpu_id});
            propagate(cpu_res);
            ++next_id;
        }

        return builder.build();
    }

    /**
     * @brief 从 cpu-map 构建 CPU 拓扑.
     */
    [[nodiscard]]
    Result<device::CpuTopology> build_cpu_map_topology(
        const fdt::Node &cpu_map_node,
        const std::unordered_map<fdt::phandle_t, device::cpuid_t>
            &cpu_phandle_map,
        const std::vector<device::cpuid_t> &all_cpu_ids) {
        // 收集顶层拓扑节点.
        auto top_children = sorted_children(cpu_map_node);
        if (top_children.empty()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        device::CpuTopologyBuilder builder;
        device::topo_t next_topo_id = 1;

        // 单根拓扑直接将该节点提升为树根.
        if (top_children.size() == 1) {
            auto root_level = topo_level_from_name(top_children.front()->name);
            if (!root_level.has_value()) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            builder.root(*root_level, 0);

            std::vector<device::cpuid_t> aggregated;
            auto cpu_it = top_children.front()->properties.find(CPU_PROP);
            if (cpu_it != top_children.front()->properties.end()) {
                fdt::phandle_t cpu_phandle = cpu_it->second->as_phandle();
                auto cpu_map_it            = cpu_phandle_map.find(cpu_phandle);
                if (cpu_map_it == cpu_phandle_map.end()) {
                    unexpect_return(ErrCode::ENTRY_NOT_FOUND);
                }
                aggregated.push_back(cpu_map_it->second);
            }

            for (const auto *child : sorted_children(*top_children.front())) {
                auto child_res = build_cpu_map_subtree(
                    builder, *child, 0, next_topo_id, cpu_phandle_map);
                propagate(child_res);
                const auto &child_cpus = child_res.value();
                aggregated.insert(aggregated.end(), child_cpus.begin(),
                                  child_cpus.end());
            }

            std::ranges::sort(aggregated);
            aggregated.erase(
                std::ranges::unique(
                    aggregated, [](device::cpuid_t lhs,
                                   device::cpuid_t rhs) { return lhs == rhs; }),
                aggregated.end());
            auto root_res = builder.cpus(0, aggregated);
            propagate(root_res);
        } else {
            // 多个顶层节点时，额外插入一个 package 根节点.
            builder.root(device::CpuTopoLevel::PACKAGE, 0);
            auto root_res = builder.cpus(0, all_cpu_ids);
            propagate(root_res);

            for (const auto *child : top_children) {
                auto child_res = build_cpu_map_subtree(
                    builder, *child, 0, next_topo_id, cpu_phandle_map);
                propagate(child_res);
            }
        }

        return builder.build();
    }

    /**
     * @brief 解析单个 CPU 节点.
     */
    [[nodiscard]]
    Result<ParsedCpu> parse_cpu_node(const fdt::Node &cpu_node) {
        // 基本状态与类型检查.
        if (!node_status_enabled(cpu_node)) {
            unexpect_return(ErrCode::BUSY);
        }
        if (!is_string_prop_equal(cpu_node, DEVICE_TYPE_PROP, CPU_DEVICE_TYPE))
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        // 解析 CPU 编号与 phandle.
        auto reg_value = parse_reg_value(cpu_node);
        if (!reg_value.has_value()) {
            loggers::DEVICE::ERROR("CPU 节点 %s 缺少 reg 属性",
                                   cpu_node.name.c_str());
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        if (*reg_value > std::numeric_limits<device::cpuid_t>::max()) {
            loggers::DEVICE::ERROR("CPU 节点 %s 的 reg 超出 cpuid 范围: %llu",
                                   cpu_node.name.c_str(),
                                   static_cast<unsigned long long>(*reg_value));
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        if (cpu_node.phandle == 0) {
            loggers::DEVICE::ERROR("CPU 节点 %s 缺少 phandle",
                                   cpu_node.name.c_str());
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        // 解析 ISA 与本地中断控制器.
        auto isa_it = cpu_node.properties.find(RISCV_ISA_PROP);
        if (isa_it == cpu_node.properties.end()) {
            loggers::DEVICE::ERROR("CPU 节点 %s 缺少 riscv,isa 属性",
                                   cpu_node.name.c_str());
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        auto local_intc = find_local_intc_phandle(cpu_node);
        if (!local_intc.has_value()) {
            loggers::DEVICE::ERROR("CPU 节点 %s 缺少本地 interrupt-controller",
                                   cpu_node.name.c_str());
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        ParsedCpu parsed{
            .id                 = static_cast<device::cpuid_t>(*reg_value),
            .model              = fallback_cpu_model(cpu_node),
            .isa_string         = isa_it->second->as_string(),
            .mmu_type           = cpu_node.properties.contains(MMU_TYPE_PROP)
                                      ? cpu_node.properties.at(MMU_TYPE_PROP)->as_string()
                                      : "",
            .cpu_phandle        = cpu_node.phandle,
            .local_intc_phandle = *local_intc,
        };
        return parsed;
    }

    /**
     * @brief 扫描并提取 CLINT 描述信息.
     */
    void find_clint_recursive(
        const fdt::Node &node,
        const std::unordered_map<fdt::phandle_t, device::cpuid_t>
            &local_intc_map,
        ClintDescriptor &descriptor) {
        if (!descriptor.found && compatible_contains(node, CLINT_COMPATIBLE)) {
            // 记录控制器基础信息.
            descriptor.found = true;
            descriptor.identifier =
                node.phandle != 0 ? static_cast<device::ictrl_t>(node.phandle)
                                  : static_cast<device::ictrl_t>(1);
            descriptor.name = node.name;

            // 解析 MMIO 区域.
            auto reg_it = node.properties.find(REG_PROP);
            if (reg_it != node.properties.end()) {
                descriptor.mmio_regions =
                    reg_it->second->as_regions(node_region_cells(node));
            } else {
                loggers::DEVICE::WARN("CLINT 节点 %s 缺少 reg 属性",
                                      node.name.c_str());
            }

            // 解析 interrupts-extended 到目标 hart 集合.
            auto ints_it = node.properties.find(INTERRUPT_EXT_PROP);
            if (ints_it != node.properties.end()) {
                auto cells = parse_u32_cells(*ints_it->second);
                if (cells.empty() || cells.size() % 2 != 0) {
                    loggers::DEVICE::ERROR(
                        "CLINT 节点 %s 的 interrupts-extended 非法",
                        node.name.c_str());
                } else {
                    for (size_t i = 0; i < cells.size(); i += 2) {
                        fdt::phandle_t phandle = cells[i];
                        auto cpu_it            = local_intc_map.find(phandle);
                        if (cpu_it == local_intc_map.end()) {
                            loggers::DEVICE::WARN(
                                "CLINT 节点 %s 引用了未知本地中断 phandle=%u",
                                node.name.c_str(), phandle);
                            continue;
                        }
                        descriptor.target_harts.push_back(cpu_it->second);
                    }
                    std::ranges::sort(descriptor.target_harts);
                    descriptor.target_harts.erase(
                        std::ranges::unique(
                            descriptor.target_harts,
                            [](device::cpuid_t lhs, device::cpuid_t rhs) {
                                return lhs == rhs;
                            }),
                        descriptor.target_harts.end());
                }
            }

            // 若 CLINT 未显式描述目标 CPU，则默认覆盖全部已知 hart.
            if (descriptor.target_harts.empty()) {
                for (const auto &[phandle, cpu_id] : local_intc_map) {
                    (void)phandle;
                    descriptor.target_harts.push_back(cpu_id);
                }
                std::ranges::sort(descriptor.target_harts);
            }
            return;
        }

        for (const auto &[_, child] : node.children) {
            find_clint_recursive(*child, local_intc_map, descriptor);
            if (descriptor.found) {
                return;
            }
        }
    }
}  // namespace

namespace fdt {
    Node *Configuration::get_node_by_path(const std::string &path) const {
        if (!root || root.get() == nullptr) {
            return nullptr;
        }
        if (path.empty() || path == "/") {
            return root.get();
        }

        Node *current    = root.get();
        size_t component = 0;
        while (component < path.size()) {
            while (component < path.size() && path[component] == '/') {
                ++component;
            }
            if (component >= path.size()) {
                break;
            }

            size_t end = component;
            while (end < path.size() && path[end] != '/') {
                ++end;
            }

            std::string name = path.substr(component, end - component);
            auto child_it    = current->children.find(name);
            if (child_it == current->children.end()) {
                return nullptr;
            }
            current   = child_it->second.get();
            component = end;
        }
        return current;
    }

    Node *Configuration::get_node_by_phandle(phandle_t phandle) const {
        auto it = phandle_map.find(phandle);
        return it == phandle_map.end() ? nullptr : it->second;
    }

    Result<void> Configuration::add_node(Node *parent,
                                         util::owner<Node *> new_node) {
        if (new_node.get() == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        if (!root) {
            if (parent != nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            root         = std::move(new_node);
            root->parent = nullptr;
            if (root->phandle != 0) {
                phandle_map[root->phandle] = root.get();
            }
            void_return();
        }

        if (parent == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        new_node->parent = parent;
        if (parent->children.contains(new_node->name)) {
            unexpect_return(ErrCode::BUSY);
        }

        if (new_node->phandle != 0) {
            phandle_map[new_node->phandle] = new_node.get();
        }
        parent->children[new_node->name] = std::move(new_node);
        void_return();
    }

    Result<void> Configuration::remove_node(Node *node) {
        if (node == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (!root || root.get() == nullptr) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        if (node == root.get()) {
            if (root->phandle != 0) {
                phandle_map.erase(root->phandle);
            }
            root = util::owner<Node *>(nullptr);
            void_return();
        }

        Node *parent = node->parent;
        if (parent == nullptr) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        auto it = parent->children.find(node->name);
        if (it == parent->children.end() || it->second.get() != node) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        if (node->phandle != 0) {
            phandle_map.erase(node->phandle);
        }
        parent->children.erase(it);
        void_return();
    }

    void make_config(void *dtb, Configuration &config) {
        config.root = util::owner<Node *>(nullptr);
        config.phandle_map.clear();

        if (dtb == nullptr) {
            assert(false && "dtb must not be null");
            return;
        }
        if (FDTHelper::fdt_init(dtb) == nullptr) {
            assert(false && "fdt_init failed");
            return;
        }

        auto root    = util::owner(new Node());
        root->name   = "/";
        root->parent = nullptr;
        build_node_recursive(static_cast<const void *>(FDTHelper::fdt), config,
                             *root, FDTHelper::get_root_node());
        config.root = std::move(root);
    }
}  // namespace fdt

namespace fdt {
    void FDTProvider::append_as_regions(
        std::vector<device::MemRegion> &regions, const RegionCells &cells,
        const Property &prop, device::MemRegion::MemoryStatus status) const {
        auto new_regions = prop.as_regions(cells);
        for (const auto &area : new_regions) {
            regions.emplace_back(area, status);
        }
    }

    bool FDTProvider::is_memory_node(Node &node) const {
        return is_string_prop_equal(node, DEVICE_TYPE_PROP, MEMORY_DEVICE_TYPE);
    }

    void FDTProvider::collect_memory_regions(
        std::vector<device::MemRegion> &regions) const {
        if (!_config.root) {
            return;
        }

        // 加入所有 device_type = memory 的节点下的区域作为FREE区域
        for (const auto &[_, node] : _config.root->children) {
            if (!node_status_enabled(*node) || !is_memory_node(*node)) {
                continue;
            }
            auto reg_it = node->properties.find(REG_PROP);
            if (reg_it == node->properties.end()) {
                loggers::DEVICE::WARN("内存节点 /%s 缺少 reg 属性, 已跳过",
                                      node->name.c_str());
                continue;
            }
            append_as_regions(regions, node_region_cells(*node),
                              *reg_it->second,
                              device::MemRegion::MemoryStatus::FREE);
        }

        // 加入 reserved-memory 下的所有保留区域
        Node *reserved_memory = _config.get_node_by_path(RESERVED_MEMORY_PATH);
        if (reserved_memory != nullptr) {
            for (const auto &[_, child] : reserved_memory->children) {
                if (!node_status_enabled(*child)) {
                    loggers::DEVICE::WARN(
                        "节点 /reserved-memory/%s 缺少 reg 属性, 已跳过",
                        child->name.c_str());
                    continue;
                }
                append_as_regions(regions, node_region_cells(*child),
                                  *child->properties.at(REG_PROP),
                                  device::MemRegion::MemoryStatus::RESERVED);
            }
        }
    }

    void FDTProvider::update_cpus(device::CpuGroupInfo &cpus) const {
        // 清理旧的 CPU 组信息.
        loggers::DEVICE::DEBUG("开始更新 CPU 组信息");
        cpus.cleanup();
        cpus.topology.cleanup();

        // 解析 /cpus 根节点与频率.
        if (!_config.root) {
            loggers::DEVICE::WARN("设备树根节点不存在, 无法获取 CPU 信息");
            return;
        }

        Node *cpus_node = _config.get_node_by_path(CPUS_PATH);
        if (cpus_node == nullptr || !node_status_enabled(*cpus_node)) {
            loggers::DEVICE::WARN(
                "设备树中缺少 /cpus 节点或其状态不可用, 无法获取 CPU 信息");
            return;
        }

        auto freq_prop_it            = cpus_node->properties.find(TIMEBASE_FREQ_PROP);
        bool clint_registered        = false;
        device::ictrl_t clint_ictrl  = device::INVALID_ICTRL_ID;
        if (freq_prop_it == cpus_node->properties.end()) {
            loggers::DEVICE::WARN(
                "节点 /cpus 缺少 timebase-frequency 属性, CPU 频率保留为 0");
        } else {
            auto timebase_freq = freq_prop_it->second->as_integral();
            if (timebase_freq == 0) {
                loggers::DEVICE::ERROR(
                    "节点 /cpus 的 timebase-frequency 为 0, 无法创建 ClockSource");
            } else {
                cpus.freq = units::frequency::from_hz(timebase_freq);
                cpus._clock_source = new device::CSRTimeClockSource(cpus.freq);
                loggers::DEVICE::INFO("已创建 CSRTimeClockSource, freq=%lluHz",
                                      static_cast<unsigned long long>(
                                          cpus.freq.to_hz()));
            }
        }

        std::vector<ParsedCpu> parsed_cpus;
        parsed_cpus.reserve(cpus_node->children.size());
        std::unordered_map<fdt::phandle_t, device::cpuid_t> cpu_phandle_map;
        std::unordered_map<fdt::phandle_t, device::cpuid_t> local_intc_map;

        // 扫描并解析每个 CPU 节点.
        for (const auto *child : sorted_children(*cpus_node)) {
            auto parsed_res = parse_cpu_node(*child);
            if (!parsed_res.has_value()) {
                if (parsed_res.error() == ErrCode::BUSY ||
                    parsed_res.error() == ErrCode::INVALID_PARAM)
                {
                    continue;
                }
                loggers::DEVICE::ERROR("解析 CPU 节点 %s 失败: %s",
                                       child->name.c_str(),
                                       to_cstring(parsed_res.error()));
                return;
            }

            const auto &parsed = parsed_res.value();
            if (cpu_phandle_map.contains(parsed.cpu_phandle)) {
                loggers::DEVICE::ERROR("CPU phandle=%u 被多个 CPU 重复使用",
                                       parsed.cpu_phandle);
                return;
            }
            if (local_intc_map.contains(parsed.local_intc_phandle)) {
                loggers::DEVICE::ERROR(
                    "本地中断 phandle=%u 被多个 CPU 重复使用",
                    parsed.local_intc_phandle);
                return;
            }
            cpu_phandle_map[parsed.cpu_phandle]       = parsed.id;
            local_intc_map[parsed.local_intc_phandle] = parsed.id;
            parsed_cpus.push_back(parsed);
        }

        if (parsed_cpus.empty()) {
            loggers::DEVICE::WARN("未在 /cpus 下解析到可用 CPU");
            return;
        }

        std::ranges::sort(parsed_cpus,
                          [](const ParsedCpu &lhs, const ParsedCpu &rhs) {
                              return lhs.id < rhs.id;
                          });

        // 扫描并注册 CLINT.
        ClintDescriptor clint_desc;
        find_clint_recursive(*_config.root, local_intc_map, clint_desc);

        if (clint_desc.found) {
            if (cpus._clock_source == nullptr) {
                loggers::DEVICE::ERROR(
                    "检测到 CLINT 节点 %s, 但缺少全局 ClockSource, 跳过 CLINT 注册",
                    clint_desc.name.c_str());
            } else if (clint_desc.target_harts.empty()) {
                loggers::DEVICE::ERROR(
                    "CLINT 节点 %s 未解析到任何目标 hart, 跳过 CLINT 注册",
                    clint_desc.name.c_str());
            } else {
                auto register_res =
                    device::DeviceModel::inst().interrupt().register_controller(
                        util::owner<device::IntCtrl *>(new device::Clint(
                            clint_desc.name.empty() ? "clint" : clint_desc.name,
                            clint_desc.identifier, clint_desc.mmio_regions,
                            clint_desc.target_harts.front())
                        ),
                        clint_desc.identifier);
                if (!register_res.has_value()) {
                    loggers::DEVICE::ERROR("注册 CLINT 失败: %s",
                                           to_cstring(register_res.error()));
                    return;
                }
                clint_registered = true;
                clint_ictrl      = clint_desc.identifier;
                loggers::DEVICE::INFO(
                    "已注册 CLINT 控制器: %s (id=%u, harts=%u)",
                    clint_desc.name.c_str(), clint_desc.identifier,
                    static_cast<unsigned>(clint_desc.target_harts.size()));
            }
        } else {
            loggers::DEVICE::WARN(
                "设备树中未找到 riscv,clint0, CPU 本地中断控制器将标记为无效");
        }

        // 构造 CPU 对象列表.
        std::vector<device::cpuid_t> cpu_ids;
        cpu_ids.reserve(parsed_cpus.size());
        for (const auto &parsed : parsed_cpus) {
            // 关联本地中断控制器 ID
            device::ictrl_t local_ctrl = clint_registered
                                             ? clint_ictrl
                                             : device::INVALID_ICTRL_ID;

            // 构建 CPU 对象
            // TODO: 我不太清楚缓存数据应如何解析出来
            auto cpu_res = device::RiscV64Cpu::Builder()
                               .id(parsed.id)
                               .model(parsed.model)
                               .frequency(cpus.freq)
                               .isa_string(parsed.isa_string)
                               .mmu_type(parsed.mmu_type)
                               .local_intc(local_ctrl)
                               .build();
            if (!cpu_res.has_value()) {
                loggers::DEVICE::ERROR("构建 CPU %u 失败: %s", parsed.id,
                                       to_cstring(cpu_res.error()));
                return;
            }
            cpu_ids.push_back(parsed.id);
            cpus.cpus.emplace_back(cpu_res.value().get());
        }

        // 构建拓扑，失败时降级为保底拓扑.
        auto cpu_map_it = cpus_node->children.find(CPU_MAP_NODE);
        Result<device::CpuTopology> topology_res =
            cpu_map_it != cpus_node->children.end()
                ? build_cpu_map_topology(*cpu_map_it->second, cpu_phandle_map,
                                         cpu_ids)
                : build_default_topology(cpu_ids);

        if (!topology_res.has_value()) {
            loggers::DEVICE::WARN("构建 cpu-map 拓扑失败: %s, 降级为默认拓扑",
                                  to_cstring(topology_res.error()));
            topology_res = build_default_topology(cpu_ids);
        }
        if (!topology_res.has_value()) {
            loggers::DEVICE::ERROR("构建默认 CPU 拓扑失败: %s",
                                   to_cstring(topology_res.error()));
            return;
        }

        // 回填最终 CPU 组信息.
        cpus.topology = std::move(topology_res.value());
        loggers::DEVICE::INFO(
            "CPU 信息更新完成: freq=%lluHz count=%u topo_cpus=%u",
            static_cast<unsigned long long>(cpus.freq.to_hz()),
            static_cast<unsigned>(cpus.cpus.size()),
            static_cast<unsigned>(cpus.topology.logical_cpus().size()));
    }
}  // namespace fdt
