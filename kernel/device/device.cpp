/**
 * @file device.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 设备抽象
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/device.h>

#include <cstring>

namespace device {
    /**
     * @brief 构造一个直接引用原始数据的属性视图.
     */
    DevicePropView::DevicePropView(PropType type, const byte *data,
                                   size_t size) noexcept
        : _type(type), _data(data), _size(size) {}

    /**
     * @brief 使用内部缓存的 region 列表构造属性视图.
     */
    DevicePropView DevicePropView::from_region_list(
        std::vector<PhyArea> regions) noexcept {
        DevicePropView view;
        view._type    = PropType::REGION_LIST;
        view._regions = std::move(regions);
        return view;
    }

    /**
     * @brief 使用内部缓存的 virq 列表构造属性视图.
     */
    DevicePropView DevicePropView::from_virq_list(
        std::function<std::vector<driver::virq_t>()> loader) noexcept {
        DevicePropView view;
        view._type        = PropType::VIRQ_LIST;
        view._virq_lazy   = true;
        view._virq_loaded = false;
        view._virq_loader = std::move(loader);
        return view;
    }

    /**
     * @brief 获取当前属性类型.
     */
    DevicePropView::PropType DevicePropView::type() const noexcept {
        return _type;
    }

    /**
     * @brief 拷贝返回属性原始字节序列.
     */
    std::vector<byte> DevicePropView::raw_bytes() const {
        if (_data == nullptr || _size == 0) {
            return {};
        }
        return {_data, _data + _size};
    }

    /**
     * @brief 以字节数组形式读取属性.
     */
    std::vector<byte> DevicePropView::as_byte_array() const {
        switch (_type) {
            case PropType::NONE:         return {};
            case PropType::BYTE_ARRAY:
            case PropType::STRING:
            case PropType::STRING_LIST:
            case PropType::INTEGER:
            case PropType::INTEGER_LIST:
            case PropType::ANY:          return raw_bytes();
            case PropType::REGION_LIST:
            case PropType::VIRQ_LIST:
            default:                     return {};
        }
    }

    /**
     * @brief 以字符串形式读取属性首项.
     */
    std::string_view DevicePropView::as_string() const {
        if (_data == nullptr || _size == 0 ||
            (_type != PropType::STRING && _type != PropType::STRING_LIST))
        {
            return {};
        }

        size_t len = 0;
        while (len < _size && _data[len] != '\0') {
            ++len;
        }
        return std::string_view(reinterpret_cast<const char *>(_data), len);
    }

    /**
     * @brief 以字符串列表形式读取属性.
     */
    std::vector<std::string_view> DevicePropView::as_string_list() const {
        std::vector<std::string_view> result;
        if (_data == nullptr || _size == 0 ||
            (_type != PropType::STRING && _type != PropType::STRING_LIST))
        {
            return result;
        }

        size_t offset = 0;
        while (offset < _size) {
            const char *entry = reinterpret_cast<const char *>(_data + offset);
            size_t remaining  = _size - offset;
            size_t len        = strnlen(entry, remaining);
            if (len == 0) {
                ++offset;
                continue;
            }
            result.emplace_back(entry, len);
            offset += len + 1;
        }
        return result;
    }

    /**
     * @brief 以大端无符号整数形式读取属性.
     */
    sus_u64 DevicePropView::as_integer(size_t cellsz) const {
        if (_data == nullptr || _size != cellsz || cellsz == 0 ||
            cellsz > sizeof(sus_u64) || _type != PropType::INTEGER)
        {
            assert(false && "invalid integer property view");
            return 0;
        }
        return parse_be_integer(_data, cellsz);
    }

    /**
     * @brief 以大端无符号整数列表形式读取属性.
     */
    std::vector<sus_u64> DevicePropView::as_integer_list(size_t cellsz) const {
        std::vector<sus_u64> result;
        if (_data == nullptr || cellsz == 0 || cellsz > sizeof(sus_u64) ||
            _size % cellsz != 0 ||
            (_type != PropType::INTEGER && _type != PropType::INTEGER_LIST))
        {
            assert(false && "invalid integer list property view");
            return result;
        }

        result.reserve(_size / cellsz);
        for (size_t offset = 0; offset < _size; offset += cellsz) {
            result.push_back(parse_be_integer(_data + offset, cellsz));
        }
        return result;
    }

    /**
     * @brief 读取结构化 MMIO 区域列表.
     */
    std::vector<PhyArea> DevicePropView::as_region_list() const {
        if (_type != PropType::REGION_LIST) {
            return {};
        }
        return _regions;
    }

    /**
     * @brief 读取结构化 virq 列表.
     */
    std::vector<driver::virq_t> DevicePropView::as_virq_list() const {
        if (_type != PropType::VIRQ_LIST) {
            return {};
        }
        if (_virq_lazy && !_virq_loaded) {
            this->_virqs       = this->_virq_loader ? this->_virq_loader()
                                                    : std::vector<driver::virq_t>{};
            this->_virq_loaded = true;
        }
        return _virqs;
    }

    /**
     * @brief 按大端序解析定长无符号整数.
     */
    uint64_t DevicePropView::parse_be_integer(const byte *data,
                                              size_t size) noexcept {
        if (data == nullptr || size == 0 || size > sizeof(uint64_t)) {
            return 0;
        }

        uint64_t value = 0;
        for (size_t index = 0; index < size; ++index) {
            value = (value << 8) | static_cast<uint64_t>(data[index]);
        }
        return value;
    }
}  // namespace device
