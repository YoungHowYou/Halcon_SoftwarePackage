#pragma once
#include <cstdint>
#include <string>

namespace cvr {

// ----------------------------------------------------------------------------
// 基本类型（与 HALCON 类型无关，内部统一 32/64 位）
// ----------------------------------------------------------------------------
using CvrCoord  = int32_t;   // 行/列坐标，内部统一 32 位
using CvrChords = int64_t;   // run 计数，对齐 HALCON 的 HITEMCNT

constexpr double CVR_INF_VAL = 1e30;
constexpr double CVR_PI      = 3.14159265358979323846;

// ----------------------------------------------------------------------------
// 错误处理：L1 纯函数返回 bool，错误信息通过 thread_local 字符串传递
// ----------------------------------------------------------------------------
const char* cvr_last_error() noexcept;
void        cvr_set_last_error(const std::string& msg) noexcept;
void        cvr_clear_last_error() noexcept;

// ----------------------------------------------------------------------------
// 特征缓存标志（与 HALCON HFeatureFlags 语义对应，但位布局独立）
// ----------------------------------------------------------------------------
struct CvrFeatureFlags {
    uint32_t shape              : 1;
    uint32_t is_convex          : 1;
    uint32_t is_filled          : 1;
    uint32_t is_connected4      : 1;
    uint32_t is_connected8      : 1;
    uint32_t is_thin            : 1;
    uint32_t circularity        : 1;
    uint32_t compactness        : 1;
    uint32_t contlength         : 1;
    uint32_t convexity          : 1;
    uint32_t phi                : 1;
    uint32_t elliptic_axis      : 1;  // ra, rb
    uint32_t elliptic_shape     : 1;  // ra_, rb_
    uint32_t excentricity       : 1;  // anisometry, bulkiness, structure_factor
    uint32_t moments            : 1;  // m11, m20, m02, ia, ib
    uint32_t center_area        : 1;  // row, col, area
    uint32_t smallest_rectangle1: 1;  // row1, col1, row2, col2
    uint32_t smallest_rectangle2: 1;  // row_rect, col_rect, phi_rect, length1, length2
    uint32_t smallest_circle    : 1;  // row_circle, col_circle, radius
    uint32_t min_max_chord      : 1;
    uint32_t min_max_chord_gap  : 1;
    uint32_t rectangularity     : 1;
    uint32_t reserved           : 10;

    CvrFeatureFlags() noexcept { *reinterpret_cast<uint32_t*>(this) = 0; }

    void reset() noexcept { *reinterpret_cast<uint32_t*>(this) = 0; }
    bool any() const noexcept { return *reinterpret_cast<const uint32_t*>(this) != 0; }
};

static_assert(sizeof(CvrFeatureFlags) == sizeof(uint32_t),
              "CvrFeatureFlags must pack into 32 bits");

} // namespace cvr
