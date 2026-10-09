/*=============================================================================
 * cv_region/python/src/bindings.cpp — pybind11 绑定（HALCON 语义 region 库 cvr_core）
 *=============================================================================
 * 设计约定（与 include/cvr/cvr.hpp 逐条对应，便于对照）：
 *   1. 模块级函数 **保持 C++ 原名**（cvr_union2 / cvr_select_shape / ...）；
 *      C++ 的「出参」在 Python 里改成「返回值」（tuple / Region / list[Region]）。
 *   2. 单区域算子另外挂到 Region 类上做方法，方法名 = 函数名去掉 `cvr_` 以及紧随的
 *      `region_` / `feature_` 前缀（因为它们已经是 Region 的方法），例如：
 *        cvr_region_normalize      -> Region.normalize()
 *        cvr_feature_area_center   -> Region.area_center()
 *        cvr_region_to_bin         -> Region.to_bin()
 *   3. 失败即抛异常：C++ 侧「bool 返回 + cvr_last_error()」在此统一转成
 *      RuntimeError(cvr_last_error())。
 *   4. 不引 <pybind11/numpy.h>：核心函数只走 **buffer 协议**
 *      （numpy 数组 / bytearray / memoryview 都收），numpy 仅在
 *      to_image / to_mask / from_image(depth=None) 里用 `import numpy` 调，
 *      所以没装 numpy 也能用全部核心功能。
 *   5. **不自动 normalize**：与 C++ 一致（cvr_region_create 也不 normalize），
 *      需要时显式调 Region.normalize()。
 *===========================================================================*/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "cvr/cvr.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

using cvr::CvrChords;
using cvr::CvrCoord;
using cvr::CvrFeature;
using cvr::CvrGrayDepth;
using cvr::CvrRegion;
using cvr::CvrRun;

namespace {

/*===========================================================================
 * 一、错误桥接
 *=========================================================================*/

//! C++ 侧「bool 返回 + thread_local 错误串」→ Python 异常
[[noreturn]] void raise_last_error(const char* what) {
    std::string msg = cvr::cvr_last_error();
    if (msg.empty()) {
        msg = std::string(what ? what : "cvr") + " 失败（C++ 侧未给出错误信息）";
    }
    throw std::runtime_error(msg);
}

void check(bool ok, const char* what) {
    if (!ok) raise_last_error(what);
}

/*===========================================================================
 * 二、灰度深度（CvrGrayDepth）：整数 / 枚举 / 字符串双写法
 *=========================================================================*/

size_t depth_itemsize(CvrGrayDepth d) {
    switch (d) {
        case cvr::CVR_GRAY_U8:  return 1;
        case cvr::CVR_GRAY_S8:  return 1;
        case cvr::CVR_GRAY_U16: return 2;
        case cvr::CVR_GRAY_S16: return 2;
        case cvr::CVR_GRAY_S32: return 4;
        case cvr::CVR_GRAY_F32: return 4;
        case cvr::CVR_GRAY_S64: return 8;
        case cvr::CVR_GRAY_F64: return 8;
    }
    return 0;
}

//! numpy dtype 名 -> CvrGrayDepth（只有 numpy 侧用得到，名字无歧义）
CvrGrayDepth depth_of_numpy_name(const std::string& n) {
    if (n == "uint8" || n == "bool") return cvr::CVR_GRAY_U8;
    if (n == "int8")                 return cvr::CVR_GRAY_S8;
    if (n == "uint16")               return cvr::CVR_GRAY_U16;
    if (n == "int16")                return cvr::CVR_GRAY_S16;
    if (n == "int32")                return cvr::CVR_GRAY_S32;
    if (n == "int64")                return cvr::CVR_GRAY_S64;
    if (n == "float32")              return cvr::CVR_GRAY_F32;
    if (n == "float64")              return cvr::CVR_GRAY_F64;
    throw py::value_error("不支持的 numpy dtype: " + n +
                          "（支持 uint8/int8/uint16/int16/int32/int64/float32/float64/bool）");
}

//! 缓冲格式字符 -> CvrGrayDepth（无 numpy 时的兜底；含端序前缀如 "<f"）
CvrGrayDepth depth_of_format(const std::string& f) {
    size_t i = 0;
    while (i < f.size() && (f[i] == '<' || f[i] == '>' || f[i] == '=' ||
                            f[i] == '!' || f[i] == '@')) {
        ++i;
    }
    if (i >= f.size()) {
        throw py::value_error("缓冲无格式信息，请显式指定 depth");
    }
    switch (f[i]) {
        case '?': case 'B': case 'c': return cvr::CVR_GRAY_U8;
        case 'b':                     return cvr::CVR_GRAY_S8;
        case 'H':                     return cvr::CVR_GRAY_U16;
        case 'h':                     return cvr::CVR_GRAY_S16;
        case 'i': case 'I':
        case 'l': case 'L':           return cvr::CVR_GRAY_S32;
        case 'q': case 'Q':           return cvr::CVR_GRAY_S64;
        case 'f':                     return cvr::CVR_GRAY_F32;
        case 'd': case 'g':           return cvr::CVR_GRAY_F64;
        default: break;
    }
    throw py::value_error("无法从缓冲格式 '" + f + "' 推断 depth，请显式指定");
}

//! depth 入参：CvrGrayDepth 枚举 / 整数 / 字符串别名（大小写不敏感）
CvrGrayDepth parse_depth(const py::object& o) {
    if (py::isinstance<CvrGrayDepth>(o)) {
        return py::cast<CvrGrayDepth>(o);
    }
    if (py::isinstance<py::str>(o)) {
        std::string s = py::cast<std::string>(o);
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        // 只收无歧义别名：HALCON 名（byte/int1/uint2/int2/int4/int8/real/double）
        // 与 numpy 名（uint8/int16/uint16/int32/int64/float32/float64）；
        // 裸 "int8" 有歧义（HALCON=int64，numpy=int8）→ 明确拒绝
        if (s == "u8"  || s == "uint8" || s == "byte")  return cvr::CVR_GRAY_U8;
        if (s == "s8"  || s == "int1")                  return cvr::CVR_GRAY_S8;
        if (s == "u16" || s == "uint16" || s == "uint2") return cvr::CVR_GRAY_U16;
        if (s == "s16" || s == "int16" || s == "int2")  return cvr::CVR_GRAY_S16;
        if (s == "s32" || s == "int32" || s == "int4")  return cvr::CVR_GRAY_S32;
        if (s == "s64" || s == "int64")                 return cvr::CVR_GRAY_S64;
        if (s == "f32" || s == "float32" || s == "real") return cvr::CVR_GRAY_F32;
        if (s == "f64" || s == "float64" || s == "double") return cvr::CVR_GRAY_F64;
        throw py::value_error(
            "未知 depth: '" + s + "'；可用 byte/u8/uint8, int1/s8, uint2/u16/uint16, "
            "int2/s16/int16, int4/s32/int32, int64/s64, real/f32/float32, "
            "double/f64/float64（注意裸 int8 有歧义，请写 int1 或 int64）");
    }
    int v = 0;
    try {
        v = py::cast<int>(o);
    } catch (const py::cast_error&) {
        throw py::type_error("depth 必须是 GrayDepth / 整数 / 字符串");
    }
    if (v < 0 || v > 7) {
        throw py::value_error("depth 整数越界: " + std::to_string(v) + "（0..7）");
    }
    return static_cast<CvrGrayDepth>(v);
}

/*===========================================================================
 * 三、灰度缓冲校验（buffer 协议，不依赖 numpy）
 *=========================================================================*/

//! 是否是 buffer（numpy 数组 / memoryview / bytearray / array.array ...）
bool is_buffer(py::handle h) {
    return h.ptr() != nullptr && PyObject_CheckBuffer(h.ptr()) != 0;
}

//! 取灰度缓冲并要求「2 维 (h,w) 或 1 维 (w*h) 的连续内存，元素宽度与 depth 一致」
//!  * 2 维：width/height 传 0 表示按数组形状推断（推断后回写 width/height）
//!  * 1 维：必须已给出 width/height
//! 返回的 buffer_info 持有 view，函数存活期间 info.ptr 有效。
py::buffer_info gray_buffer(const py::object& gray, CvrGrayDepth d,
                            int& width, int& height, bool writable) {
    if (!is_buffer(gray)) {
        throw py::type_error("灰度缓冲必须是支持 buffer 协议的对象"
                             "（numpy 数组 / memoryview / bytearray / array.array）");
    }
    py::buffer_info info = py::reinterpret_borrow<py::buffer>(gray).request(writable);
    const size_t esz = depth_itemsize(d);
    if (static_cast<size_t>(info.itemsize) != esz) {
        throw py::value_error("灰度缓冲元素宽度 = " + std::to_string(info.itemsize) +
                              " 字节，与 depth（" + std::to_string(esz) + " 字节）不符");
    }
    if (info.ndim == 2) {
        const int h2 = static_cast<int>(info.shape[0]);
        const int w2 = static_cast<int>(info.shape[1]);
        if (width > 0 && width != w2) {
            throw py::value_error("灰度缓冲宽度 " + std::to_string(w2) +
                                  " 与 width=" + std::to_string(width) + " 不符");
        }
        if (height > 0 && height != h2) {
            throw py::value_error("灰度缓冲高度 " + std::to_string(h2) +
                                  " 与 height=" + std::to_string(height) + " 不符");
        }
        if (info.strides[0] != static_cast<py::ssize_t>(w2) * static_cast<py::ssize_t>(esz) ||
            info.strides[1] != static_cast<py::ssize_t>(esz)) {
            throw py::value_error("灰度缓冲必须 C 连续"
                                  "（numpy 可用 numpy.ascontiguousarray 转换）");
        }
        width = w2;
        height = h2;
    } else if (info.ndim == 1) {
        if (width <= 0 || height <= 0) {
            throw py::value_error("一维灰度缓冲必须同时给出 width 与 height");
        }
        if (info.shape[0] != static_cast<py::ssize_t>(width) *
                                 static_cast<py::ssize_t>(height)) {
            throw py::value_error("一维灰度缓冲长度必须等于 width*height");
        }
        if (info.strides[0] != static_cast<py::ssize_t>(esz)) {
            throw py::value_error("灰度缓冲必须连续");
        }
    } else {
        throw py::value_error("灰度缓冲必须是 1 维或 2 维");
    }
    return info;
}

/*===========================================================================
 * 四、Region / Run 数据装载
 *=========================================================================*/

//! runs 序列 -> CvrRegion::runs；元素可以是 Run，也可以是 (r, cb, ce) 三元组
void assign_runs(CvrRegion& r, const py::object& runs) {
    r.runs.clear();
    if (runs.is_none()) return;
    for (py::handle item : py::reinterpret_borrow<py::iterable>(runs)) {
        if (py::isinstance<CvrRun>(item)) {
            r.runs.push_back(py::cast<CvrRun>(item));
            continue;
        }
        py::object o = py::reinterpret_borrow<py::object>(item);
        py::sequence seq = py::cast<py::sequence>(o);
        if (py::len(seq) != 3) {
            throw py::value_error("run 必须是 Run 对象或 (r, cb, ce) 三元组");
        }
        r.runs.push_back(CvrRun{py::cast<CvrCoord>(seq[0]),
                                py::cast<CvrCoord>(seq[1]),
                                py::cast<CvrCoord>(seq[2])});
    }
    cvr::cvr_region_invalidate(r);   // 与 C++ 约定一致：改完 runs 必须作废特征缓存
}

std::string run_repr(const CvrRun& x) {
    return "Run(r=" + std::to_string(x.r) + ", cb=" + std::to_string(x.cb) +
           ", ce=" + std::to_string(x.ce) + ")";
}

} // namespace

/*===========================================================================
 * 模块定义
 *=========================================================================*/
PYBIND11_MODULE(_cvr, m) {
    m.doc() = "cv_region：HALCON 语义的游程(RLE) region 库 cvr_core 的 Python 绑定";
    m.attr("__cvr_version__") = "0.2.0";   // 与 cvr.hpp / CMake 版本同步

    /*-----------------------------------------------------------------------
     * 常量（cvr.hpp 顶层 + 形状名 + 特征位）
     *---------------------------------------------------------------------*/
    m.attr("CVR_INF_VAL") = cvr::CVR_INF_VAL;
    m.attr("CVR_PI")      = cvr::CVR_PI;

    m.attr("CVR_SHAPE_RECTANGLE1")   = cvr::CVR_SHAPE_RECTANGLE1;
    m.attr("CVR_SHAPE_RECTANGLE2")   = cvr::CVR_SHAPE_RECTANGLE2;
    m.attr("CVR_SHAPE_ELLIPSE")      = cvr::CVR_SHAPE_ELLIPSE;
    m.attr("CVR_SHAPE_OUTER_CIRCLE") = cvr::CVR_SHAPE_OUTER_CIRCLE;
    m.attr("CVR_SHAPE_CONVEX")       = cvr::CVR_SHAPE_CONVEX;

    m.attr("CVR_FC_NONE")           = static_cast<uint32_t>(cvr::CVR_FC_NONE);
    m.attr("CVR_FC_CENTER_AREA")    = static_cast<uint32_t>(cvr::CVR_FC_CENTER_AREA);
    m.attr("CVR_FC_MOMENTS")        = static_cast<uint32_t>(cvr::CVR_FC_MOMENTS);
    m.attr("CVR_FC_ELLIPTIC_AXIS")  = static_cast<uint32_t>(cvr::CVR_FC_ELLIPTIC_AXIS);
    m.attr("CVR_FC_EXCENTRICITY")   = static_cast<uint32_t>(cvr::CVR_FC_EXCENTRICITY);
    m.attr("CVR_FC_CONT_LENGTH")    = static_cast<uint32_t>(cvr::CVR_FC_CONT_LENGTH);
    m.attr("CVR_FC_CONVEXITY")      = static_cast<uint32_t>(cvr::CVR_FC_CONVEXITY);
    m.attr("CVR_FC_CIRCULARITY")    = static_cast<uint32_t>(cvr::CVR_FC_CIRCULARITY);
    m.attr("CVR_FC_COMPACTNESS")    = static_cast<uint32_t>(cvr::CVR_FC_COMPACTNESS);
    m.attr("CVR_FC_RECTANGULARITY") = static_cast<uint32_t>(cvr::CVR_FC_RECTANGULARITY);
    m.attr("CVR_FC_RECTANGLE1")     = static_cast<uint32_t>(cvr::CVR_FC_RECTANGLE1);
    m.attr("CVR_FC_RECTANGLE2")     = static_cast<uint32_t>(cvr::CVR_FC_RECTANGLE2);
    m.attr("CVR_FC_CIRCLE")         = static_cast<uint32_t>(cvr::CVR_FC_CIRCLE);
    m.attr("CVR_FC_BASIC")          = static_cast<uint32_t>(cvr::CVR_FC_BASIC);
    m.attr("CVR_FC_HULL")           = static_cast<uint32_t>(cvr::CVR_FC_HULL);
    m.attr("CVR_FC_ALL")            = static_cast<uint32_t>(cvr::CVR_FC_ALL);

    /*-----------------------------------------------------------------------
     * GrayDepth 枚举（同时给出模块级整数常量 CVR_GRAY_*，与 C++ 同名）
     *---------------------------------------------------------------------*/
    py::enum_<CvrGrayDepth>(m, "GrayDepth",
                            "像素深度（与 CvrGrayDepth 同值；也可直接传整数或字符串别名）")
        .value("U8",  cvr::CVR_GRAY_U8,  "uint8_t  (byte / direction / cyclic)")
        .value("S8",  cvr::CVR_GRAY_S8,  "int8_t   (int1)")
        .value("U16", cvr::CVR_GRAY_U16, "uint16_t (uint2)")
        .value("S16", cvr::CVR_GRAY_S16, "int16_t  (int2)")
        .value("S32", cvr::CVR_GRAY_S32, "int32_t  (int4)")
        .value("S64", cvr::CVR_GRAY_S64, "int64_t  (int8)")
        .value("F32", cvr::CVR_GRAY_F32, "float    (real)")
        .value("F64", cvr::CVR_GRAY_F64, "double   (real, double 缓冲)");

    m.attr("CVR_GRAY_U8")  = static_cast<int>(cvr::CVR_GRAY_U8);
    m.attr("CVR_GRAY_S8")  = static_cast<int>(cvr::CVR_GRAY_S8);
    m.attr("CVR_GRAY_U16") = static_cast<int>(cvr::CVR_GRAY_U16);
    m.attr("CVR_GRAY_S16") = static_cast<int>(cvr::CVR_GRAY_S16);
    m.attr("CVR_GRAY_S32") = static_cast<int>(cvr::CVR_GRAY_S32);
    m.attr("CVR_GRAY_S64") = static_cast<int>(cvr::CVR_GRAY_S64);
    m.attr("CVR_GRAY_F32") = static_cast<int>(cvr::CVR_GRAY_F32);
    m.attr("CVR_GRAY_F64") = static_cast<int>(cvr::CVR_GRAY_F64);

    /*-----------------------------------------------------------------------
     * Run：单条 chord/run（HALCON Hrun 语义，闭区间 [cb, ce]）
     *---------------------------------------------------------------------*/
    py::class_<CvrRun>(m, "Run", "单条游程：行 r，列区间 [cb, ce]（闭区间）")
        .def(py::init([]() { return CvrRun{0, 0, 0}; }))
        .def(py::init([](CvrCoord r, CvrCoord cb, CvrCoord ce) {
                 return CvrRun{r, cb, ce};
             }),
             py::arg("r"), py::arg("cb"), py::arg("ce"))
        .def_readwrite("r",  &CvrRun::r,  "行号")
        .def_readwrite("cb", &CvrRun::cb, "起始列（包含）")
        .def_readwrite("ce", &CvrRun::ce, "结束列（包含）")
        .def_property_readonly("length", [](const CvrRun& x) {
            return static_cast<CvrCoord>(x.ce - x.cb + 1);
        }, "该游程像素数（ce - cb + 1）")
        .def("__len__", [](const CvrRun&) { return 3; })
        .def("__iter__", [](const CvrRun& x) {
            return py::iter(py::make_tuple(x.r, x.cb, x.ce));
        }, "支持 tuple(run) / r, cb, ce = run 解包")
        .def("__eq__", [](const CvrRun& a, const CvrRun& b) {
            return a.r == b.r && a.cb == b.cb && a.ce == b.ce;
        }, py::is_operator())
        .def("__repr__", [](const CvrRun& x) { return run_repr(x); });

    /*-----------------------------------------------------------------------
     * Feature：惰性特征缓存内容（只读快照）
     *---------------------------------------------------------------------*/
    py::class_<CvrFeature>(m, "Feature",
                           "Region 的特征缓存快照（只读；flags 位表示对应字段有效）")
        .def_property_readonly("flags", [](const CvrFeature& f) {
            return f.flags.raw();
        }, "CvrFeatureFlags 的原始位（见 CVR_FC_* 常量）")
        .def_readonly("shape",             &CvrFeature::shape)
        .def_readonly("is_convex",         &CvrFeature::is_convex)
        .def_readonly("is_filled",         &CvrFeature::is_filled)
        .def_readonly("is_connected4",     &CvrFeature::is_connected4)
        .def_readonly("is_connected8",     &CvrFeature::is_connected8)
        .def_readonly("is_thin",           &CvrFeature::is_thin)
        .def_readonly("circularity",       &CvrFeature::circularity)
        .def_readonly("compactness",       &CvrFeature::compactness)
        .def_readonly("contlength",        &CvrFeature::contlength)
        .def_readonly("convexity",         &CvrFeature::convexity)
        .def_readonly("phi",               &CvrFeature::phi)
        .def_readonly("ra",                &CvrFeature::ra)
        .def_readonly("rb",                &CvrFeature::rb)
        .def_readonly("ra_",               &CvrFeature::ra_)
        .def_readonly("rb_",               &CvrFeature::rb_)
        .def_readonly("anisometry",        &CvrFeature::anisometry)
        .def_readonly("bulkiness",         &CvrFeature::bulkiness)
        .def_readonly("structure_factor",  &CvrFeature::structure_factor)
        .def_readonly("m11",               &CvrFeature::m11)
        .def_readonly("m20",               &CvrFeature::m20)
        .def_readonly("m02",               &CvrFeature::m02)
        .def_readonly("ia",                &CvrFeature::ia)
        .def_readonly("ib",                &CvrFeature::ib)
        .def_readonly("row",               &CvrFeature::row)
        .def_readonly("col",               &CvrFeature::col)
        .def_readonly("area",              &CvrFeature::area)
        .def_readonly("row1",              &CvrFeature::row1)
        .def_readonly("col1",              &CvrFeature::col1)
        .def_readonly("row2",              &CvrFeature::row2)
        .def_readonly("col2",              &CvrFeature::col2)
        .def_readonly("row_rect",          &CvrFeature::row_rect)
        .def_readonly("col_rect",          &CvrFeature::col_rect)
        .def_readonly("phi_rect",          &CvrFeature::phi_rect)
        .def_readonly("length1",           &CvrFeature::length1)
        .def_readonly("length2",           &CvrFeature::length2)
        .def_readonly("row_circle",        &CvrFeature::row_circle)
        .def_readonly("col_circle",        &CvrFeature::col_circle)
        .def_readonly("radius",            &CvrFeature::radius)
        .def_readonly("min_chord",         &CvrFeature::min_chord)
        .def_readonly("max_chord",         &CvrFeature::max_chord)
        .def_readonly("min_chord_gap",     &CvrFeature::min_chord_gap)
        .def_readonly("max_chord_gap",     &CvrFeature::max_chord_gap)
        .def_readonly("rectangularity",    &CvrFeature::rectangularity)
        .def("__repr__", [](const CvrFeature& f) {
            return "Feature(flags=0x" + [&f] {
                       static const char* hx = "0123456789abcdef";
                       std::string s(8, '0');
                       uint32_t v = f.flags.raw();
                       for (int i = 7; i >= 0; --i) { s[i] = hx[v & 0xf]; v >>= 4; }
                       return s;
                   }() + ")";
        });

    /*-----------------------------------------------------------------------
     * Region：本体（runs + is_compl + 惰性特征缓存）
     *---------------------------------------------------------------------*/
    py::class_<CvrRegion> region(m, "Region",
        "RLE 游程编码的 region（HALCON region 语义：runs + is_compl + 惰性特征缓存）。\n\n"
        "构造：Region(runs=None, is_compl=False)，runs 元素为 Run 或 (r, cb, ce)。\n"
        "注意（与 C++ 一致）：**不自动 normalize**，需要时显式调 .normalize()。");

    region
        .def(py::init([](py::object runs, bool is_compl) {
                 CvrRegion r;
                 r.is_compl = is_compl;
                 assign_runs(r, runs);
                 return r;
             }),
             py::arg("runs") = py::none(), py::arg("is_compl") = false)
        .def_property("runs",
            [](const CvrRegion& r) { return r.runs; },
            [](CvrRegion& r, const py::object& v) { assign_runs(r, v); },
            "游程列表（list[Run]；赋值时收 Run 或 (r, cb, ce)；赋值会作废特征缓存）")
        .def_property("is_compl",
            [](const CvrRegion& r) { return r.is_compl; },
            [](CvrRegion& r, bool v) {
                r.is_compl = v;
                cvr::cvr_region_invalidate(r);
            },
            "是否为补集（is_compl；赋值会作废特征缓存）")
        .def_property_readonly("feature", [](const CvrRegion& r) {
            return r.feature;
        }, "特征缓存快照（由各 feature_* / precompute_features 填充）")
        .def_property_readonly("num_runs", [](const CvrRegion& r) {
            return static_cast<py::ssize_t>(r.runs.size());
        }, "游程条数")
        .def("__len__", [](const CvrRegion& r) {
            return static_cast<py::ssize_t>(r.runs.size());
        })
        .def("__bool__", [](const CvrRegion& r) {
            return !r.runs.empty() || r.is_compl;
        })
        .def("__eq__", [](const CvrRegion& a, const CvrRegion& b) {
            if (a.is_compl != b.is_compl) return false;
            if (a.runs.size() != b.runs.size()) return false;
            for (size_t i = 0; i < a.runs.size(); ++i) {
                if (a.runs[i].r != b.runs[i].r || a.runs[i].cb != b.runs[i].cb ||
                    a.runs[i].ce != b.runs[i].ce) return false;
            }
            return true;
        }, py::is_operator())
        .def("__repr__", [](const CvrRegion& r) {
            return "Region(runs=" + std::to_string(r.runs.size()) +
                   ", is_compl=" + (r.is_compl ? "True" : "False") + ")";
        })
        .def("copy", [](const CvrRegion& r) { return r; }, "拷贝一整个 region（含缓存）")

        /* --- 不变量维护（cvr_region_*） --- */
        .def("invalidate", [](CvrRegion& r) { cvr::cvr_region_invalidate(r); },
             "cvr_region_invalidate：作废惰性特征缓存")
        .def("normalize", [](CvrRegion& r) {
                 check(cvr::cvr_region_normalize(r), "cvr_region_normalize");
             }, "cvr_region_normalize：原地排序并合并相邻/重叠 runs")
        .def("is_empty", [](const CvrRegion& r, CvrCoord w, CvrCoord h) {
                 return cvr::cvr_region_is_empty(r, w, h);
             }, py::arg("w"), py::arg("h"),
             "cvr_region_is_empty：是否为空（考虑 is_compl，需定义域 w,h）")
        .def("materialize", [](const CvrRegion& r, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_region_materialize(r, w, h, out), "cvr_region_materialize");
                 return out;
             }, py::arg("w"), py::arg("h"),
             "cvr_region_materialize：把补集展开成普通 runs（结果 is_compl=False）")
        .def("bbox", [](const CvrRegion& r) {
                 CvrCoord r1 = 0, c1 = 0, r2 = 0, c2 = 0;
                 check(cvr::cvr_region_bbox(r, r1, c1, r2, c2), "cvr_region_bbox");
                 return py::make_tuple(r1, c1, r2, c2);
             }, "cvr_region_bbox -> (row1, col1, row2, col2)")
        .def("row_ranges", [](const CvrRegion& r, CvrCoord row) {
                 size_t begin = 0, end = 0;
                 check(cvr::cvr_region_row_ranges(r, row, begin, end),
                       "cvr_region_row_ranges");
                 return py::make_tuple(static_cast<py::ssize_t>(begin),
                                       static_cast<py::ssize_t>(end));
             }, py::arg("row"),
             "cvr_region_row_ranges -> (begin, end)：该行 run 在 runs 里的索引区间 [begin,end)")
        .def("check_invariants", [](const CvrRegion& r) {
                 return cvr::cvr_region_check_invariants(r);
             }, "cvr_region_check_invariants：调试用，检查 chord 三条件")

        /* --- 集合代数（cvr_*） --- */
        .def("union2", [](const CvrRegion& a, const CvrRegion& b, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_union2(a, b, w, h, out), "cvr_union2");
                 return out;
             }, py::arg("r2"), py::arg("w"), py::arg("h"), "cvr_union2")
        .def("intersection", [](const CvrRegion& a, const CvrRegion& b, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_intersection(a, b, w, h, out), "cvr_intersection");
                 return out;
             }, py::arg("r2"), py::arg("w"), py::arg("h"), "cvr_intersection")
        .def("difference", [](const CvrRegion& a, const CvrRegion& b, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_difference(a, b, w, h, out), "cvr_difference");
                 return out;
             }, py::arg("r2"), py::arg("w"), py::arg("h"), "cvr_difference（self - r2）")
        .def("symm_difference", [](const CvrRegion& a, const CvrRegion& b, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_symm_difference(a, b, w, h, out), "cvr_symm_difference");
                 return out;
             }, py::arg("r2"), py::arg("w"), py::arg("h"), "cvr_symm_difference")
        .def("complement", [](const CvrRegion& r) {
                 CvrRegion out;
                 check(cvr::cvr_complement(r, out), "cvr_complement");
                 return out;
             }, "cvr_complement：只翻转 is_compl 标记（需再用才带定义域）")

        /* --- 连通域（cvr_connection） --- */
        .def("connection", [](const CvrRegion& r, int connectivity, CvrCoord w, CvrCoord h) {
                 std::vector<CvrRegion> out;
                 check(cvr::cvr_connection(r, connectivity, w, h, out), "cvr_connection");
                 return out;
             }, py::arg("connectivity") = 8, py::arg("w"), py::arg("h"),
             "cvr_connection -> list[Region]（connectivity 4 或 8，默认 8 同 HALCON）")

        /* --- 形态学（cvr_se_* / cvr_*1 / opening / closing / fill_up） --- */
        .def("se_transpose", [](const CvrRegion& se) {
                 CvrRegion out;
                 check(cvr::cvr_se_transpose(se, out), "cvr_se_transpose");
                 return out;
             }, "cvr_se_transpose")
        .def("dilation1", [](const CvrRegion& r, const CvrRegion& se, int iterations,
                             CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_dilation1(r, se, iterations, w, h, out), "cvr_dilation1");
                 return out;
             }, py::arg("se"), py::arg("iterations"), py::arg("w"), py::arg("h"), "cvr_dilation1")
        .def("erosion1", [](const CvrRegion& r, const CvrRegion& se, int iterations,
                            CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_erosion1(r, se, iterations, w, h, out), "cvr_erosion1");
                 return out;
             }, py::arg("se"), py::arg("iterations"), py::arg("w"), py::arg("h"), "cvr_erosion1")
        .def("opening", [](const CvrRegion& r, const CvrRegion& se, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_opening(r, se, w, h, out), "cvr_opening");
                 return out;
             }, py::arg("se"), py::arg("w"), py::arg("h"), "cvr_opening")
        .def("closing", [](const CvrRegion& r, const CvrRegion& se, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_closing(r, se, w, h, out), "cvr_closing");
                 return out;
             }, py::arg("se"), py::arg("w"), py::arg("h"), "cvr_closing")
        .def("fill_up", [](const CvrRegion& r, CvrCoord w, CvrCoord h) {
                 CvrRegion out;
                 check(cvr::cvr_fill_up(r, w, h, out), "cvr_fill_up");
                 return out;
             }, py::arg("w"), py::arg("h"), "cvr_fill_up（补孔洞）")

        /* --- 形状特征（cvr_feature_*） --- */
        .def("area_center", [](const CvrRegion& r) {
                 double row = 0, col = 0;
                 CvrChords area = 0;
                 check(cvr::cvr_feature_area_center(r, row, col, area),
                       "cvr_feature_area_center");
                 return py::make_tuple(row, col, static_cast<py::ssize_t>(area));
             }, "cvr_feature_area_center -> (row, col, area)")
        .def("moments", [](const CvrRegion& r) {
                 double m11 = 0, m20 = 0, m02 = 0;
                 check(cvr::cvr_feature_moments(r, m11, m20, m02), "cvr_feature_moments");
                 return py::make_tuple(m11, m20, m02);
             }, "cvr_feature_moments -> (m11, m20, m02)")
        .def("elliptic_axis", [](const CvrRegion& r) {
                 double ra = 0, rb = 0, phi = 0;
                 check(cvr::cvr_feature_elliptic_axis(r, ra, rb, phi),
                       "cvr_feature_elliptic_axis");
                 return py::make_tuple(ra, rb, phi);
             }, "cvr_feature_elliptic_axis -> (ra, rb, phi)")
        .def("contlength", [](const CvrRegion& r) {
                 double v = 0;
                 check(cvr::cvr_feature_contlength(r, v), "cvr_feature_contlength");
                 return v;
             }, "cvr_feature_contlength -> float")
        .def("convexity", [](const CvrRegion& r) {
                 double v = 0;
                 bool is_convex = false;
                 check(cvr::cvr_feature_convexity(r, v, is_convex), "cvr_feature_convexity");
                 return py::make_tuple(v, is_convex);
             }, "cvr_feature_convexity -> (convexity, is_convex)")
        .def("smallest_rectangle1", [](const CvrRegion& r) {
                 CvrCoord r1 = 0, c1 = 0, r2 = 0, c2 = 0;
                 check(cvr::cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2),
                       "cvr_feature_smallest_rectangle1");
                 return py::make_tuple(r1, c1, r2, c2);
             }, "cvr_feature_smallest_rectangle1 -> (row1, col1, row2, col2)")
        .def("smallest_rectangle2", [](const CvrRegion& r) {
                 double row = 0, col = 0, phi = 0, l1 = 0, l2 = 0;
                 check(cvr::cvr_feature_smallest_rectangle2(r, row, col, phi, l1, l2),
                       "cvr_feature_smallest_rectangle2");
                 return py::make_tuple(row, col, phi, l1, l2);
             }, "cvr_feature_smallest_rectangle2 -> (row, col, phi, length1, length2)")
        .def("smallest_circle", [](const CvrRegion& r) {
                 double row = 0, col = 0, radius = 0;
                 check(cvr::cvr_feature_smallest_circle(r, row, col, radius),
                       "cvr_feature_smallest_circle");
                 return py::make_tuple(row, col, radius);
             }, "cvr_feature_smallest_circle -> (row, col, radius)")
        .def("compactness", [](const CvrRegion& r) {
                 double v = 0;
                 check(cvr::cvr_feature_compactness(r, v), "cvr_feature_compactness");
                 return v;
             }, "cvr_feature_compactness -> float")
        .def("circularity", [](const CvrRegion& r) {
                 double v = 0;
                 check(cvr::cvr_feature_circularity(r, v), "cvr_feature_circularity");
                 return v;
             }, "cvr_feature_circularity -> float")
        .def("rectangularity", [](const CvrRegion& r) {
                 double v = 0;
                 check(cvr::cvr_feature_rectangularity(r, v), "cvr_feature_rectangularity");
                 return v;
             }, "cvr_feature_rectangularity -> float")
        .def("get_feature", [](const CvrRegion& r, const std::string& name) {
                 double v = 0;
                 check(cvr::cvr_get_feature(r, name, v), "cvr_get_feature");
                 return v;
             }, py::arg("name"), "cvr_get_feature -> float（HALCON 风格特征名）")
        .def("precompute_features", [](const CvrRegion& r, uint32_t mask) {
                 return cvr::cvr_region_precompute_features(r, mask);
             }, py::arg("mask"),
             "cvr_region_precompute_features -> 本次新置位的位数（掩码见 CVR_FC_*）")

        /* --- 灰度特征（cvr_gray_feature*） --- */
        .def("gray_feature", [](const CvrRegion& r, const py::object& gray, int width,
                                int height, const py::object& depth, const std::string& name) {
                 CvrGrayDepth d = parse_depth(depth);
                 int w = width, h = height;
                 py::buffer_info info = gray_buffer(gray, d, w, h, false);
                 double v = 0;
                 check(cvr::cvr_gray_feature(r, info.ptr, w, h, d, name, v),
                       "cvr_gray_feature");
                 return v;
             }, py::arg("gray"), py::arg("width"), py::arg("height"), py::arg("depth"),
                py::arg("name"), "cvr_gray_feature -> float（area/row/column/mean/...）")
        .def("gray_features", [](const CvrRegion& r, const py::object& gray, int width,
                                 int height, const py::object& depth,
                                 const std::vector<std::string>& names) {
                 CvrGrayDepth d = parse_depth(depth);
                 int w = width, h = height;
                 py::buffer_info info = gray_buffer(gray, d, w, h, false);
                 std::vector<CvrRegion> regs{r};
                 std::vector<double> values;
                 check(cvr::cvr_gray_features(regs, info.ptr, w, h, d, names, values),
                       "cvr_gray_features");
                 py::list out;
                 for (double v : values) out.append(v);
                 return out;
             }, py::arg("gray"), py::arg("width"), py::arg("height"), py::arg("depth"),
                py::arg("names"), "cvr_gray_features -> list[float]（本 region 的一行）")

        /* --- 形状变换 / 图像互转 --- */
        .def("shape_trans", [](const CvrRegion& r, const std::string& shape) {
                 CvrRegion out;
                 check(cvr::cvr_shape_trans(r, shape, out), "cvr_shape_trans");
                 return out;
             }, py::arg("shape"), "cvr_shape_trans（rectangle1/rectangle2/ellipse/outer_circle/convex）")
        .def("to_bin", [](const CvrRegion& r, CvrCoord w, CvrCoord h,
                          const py::object& gray, const py::object& depth,
                          double foreground, double background) {
                 CvrGrayDepth d = parse_depth(depth);
                 int ww = w, hh = h;
                 py::buffer_info info = gray_buffer(gray, d, ww, hh, true);
                 check(cvr::cvr_region_to_bin(r, ww, hh, info.ptr, d, foreground, background),
                       "cvr_region_to_bin");
             }, py::arg("w"), py::arg("h"), py::arg("gray"), py::arg("depth"),
                py::arg("foreground"), py::arg("background"),
                "cvr_region_to_bin：把 region 画进调用方提供的可写缓冲（区内 fg、区外 bg）")

        /* --- 便捷层（非 1:1，见 README"Pythonic 便捷方法"） --- */
        .def("to_bin_bytes", [](const CvrRegion& r, CvrCoord w, CvrCoord h,
                                double foreground, double background) {
                 std::string s(static_cast<size_t>(w) * static_cast<size_t>(h), '\0');
                 check(cvr::cvr_region_to_bin(r, w, h, s.empty() ? nullptr : &s[0],
                                              cvr::CVR_GRAY_U8, foreground, background),
                       "cvr_region_to_bin");
                 return py::bytes(s);
             }, py::arg("w"), py::arg("h"), py::arg("foreground") = 255.0,
                py::arg("background") = 0.0,
                "uint8 行优先缓冲（bytes，长度 w*h；不需要 numpy）")
        .def("to_image", [](const CvrRegion& r, CvrCoord w, CvrCoord h,
                            const py::object& dtype, double foreground, double background) {
                 py::object np = py::module_::import("numpy");
                 py::object arr = np.attr("zeros")(py::make_tuple(h, w),
                                                   py::arg("dtype") = dtype);
                 const std::string dname =
                     py::cast<std::string>(arr.attr("dtype").attr("name"));
                 CvrGrayDepth d = depth_of_numpy_name(dname);
                 int ww = static_cast<int>(w), hh = static_cast<int>(h);
                 py::buffer_info info = gray_buffer(arr, d, ww, hh, true);
                 check(cvr::cvr_region_to_bin(r, ww, hh, info.ptr, d, foreground, background),
                       "cvr_region_to_bin");
                 return arr;
             }, py::arg("w"), py::arg("h"), py::arg("dtype") = "uint8",
                py::arg("foreground") = 255.0, py::arg("background") = 0.0,
                "画成 numpy 数组（dtype: uint8/int8/uint16/int16/int32/int64/float32/float64）")
        .def("to_mask", [](const CvrRegion& r, CvrCoord w, CvrCoord h) {
                 py::object np = py::module_::import("numpy");
                 py::object arr = np.attr("zeros")(py::make_tuple(h, w),
                                                   py::arg("dtype") = "uint8");
                 int ww = static_cast<int>(w), hh = static_cast<int>(h);
                 py::buffer_info info = gray_buffer(arr, cvr::CVR_GRAY_U8, ww, hh, true);
                 check(cvr::cvr_region_to_bin(r, ww, hh, info.ptr, cvr::CVR_GRAY_U8,
                                              255.0, 0.0), "cvr_region_to_bin");
                 return arr;
             }, py::arg("w"), py::arg("h"),
             "numpy uint8 二值掩码（前景 255 / 背景 0），等价 to_image(w,h,'uint8',255,0)");

    /* --- 4 个 classmethod：从 mask / 图像来 --- */
    region
        .def_static("from_mask", [](const py::object& mask) {
                 py::object src = mask;
                 CvrGrayDepth d;
                 if (is_buffer(src)) {
                     py::buffer_info probe =
                         py::reinterpret_borrow<py::buffer>(src).request(false);
                     d = depth_of_format(probe.format);
                 } else {
                     src = py::module_::import("numpy").attr("ascontiguousarray")(mask);
                     d = depth_of_numpy_name(
                         py::cast<std::string>(src.attr("dtype").attr("name")));
                 }
                 int w = 0, h = 0;
                 py::buffer_info info = gray_buffer(src, d, w, h, false);
                 CvrRegion out;
                 // 非零即前景：0/1 与 0/255 两种掩码都成立（负值请改用 from_image）
                 check(cvr::cvr_bin_to_region(info.ptr, w, h, d, 0.5, out),
                       "cvr_bin_to_region");
                 return out;
             }, py::arg("mask"),
             "从二值掩码构造（非零即前景；numpy 数组 / 2 维 buffer 均可）")
        .def_static("from_image", [](const py::object& img, double threshold,
                                     const py::object& depth) {
                 py::object src = img;
                 CvrGrayDepth d;
                 if (depth.is_none()) {
                     src = py::module_::import("numpy").attr("ascontiguousarray")(img);
                     d = depth_of_numpy_name(
                         py::cast<std::string>(src.attr("dtype").attr("name")));
                 } else {
                     d = parse_depth(depth);
                 }
                 int w = 0, h = 0;
                 py::buffer_info info = gray_buffer(src, d, w, h, false);
                 CvrRegion out;
                 check(cvr::cvr_bin_to_region(info.ptr, w, h, d, threshold, out),
                       "cvr_bin_to_region");
                 return out;
             }, py::arg("img"), py::arg("threshold"), py::arg("depth") = py::none(),
             "从灰度图按 gray >= threshold 构造（depth=None 时按 numpy dtype 推断，需 numpy）");

    /*-----------------------------------------------------------------------
     * 模块级函数（保持 C++ 原名）
     *---------------------------------------------------------------------*/
    m.def("cvr_last_error", []() { return std::string(cvr::cvr_last_error()); },
          "cvr_last_error：最近一次错误的文本（线程局部）");
    m.def("cvr_set_last_error", [](const std::string& m2) {
              cvr::cvr_set_last_error(m2);
          }, py::arg("msg"), "cvr_set_last_error");
    m.def("cvr_clear_last_error", []() { cvr::cvr_clear_last_error(); },
          "cvr_clear_last_error");

    /* --- 集合代数 --- */
    m.def("cvr_union1", [](const std::vector<CvrRegion>& regions, CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_union1(regions, w, h, out), "cvr_union1");
              return out;
          }, py::arg("regions"), py::arg("w"), py::arg("h"), "cvr_union1 -> Region");
    m.def("cvr_union2", [](const CvrRegion& a, const CvrRegion& b, CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_union2(a, b, w, h, out), "cvr_union2");
              return out;
          }, py::arg("r1"), py::arg("r2"), py::arg("w"), py::arg("h"), "cvr_union2 -> Region");
    m.def("cvr_intersection", [](const CvrRegion& a, const CvrRegion& b,
                                 CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_intersection(a, b, w, h, out), "cvr_intersection");
              return out;
          }, py::arg("r1"), py::arg("r2"), py::arg("w"), py::arg("h"),
             "cvr_intersection -> Region");
    m.def("cvr_difference", [](const CvrRegion& a, const CvrRegion& b,
                               CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_difference(a, b, w, h, out), "cvr_difference");
              return out;
          }, py::arg("r1"), py::arg("r2"), py::arg("w"), py::arg("h"),
             "cvr_difference -> Region（r1 - r2）");
    m.def("cvr_complement", [](const CvrRegion& r) {
              CvrRegion out;
              check(cvr::cvr_complement(r, out), "cvr_complement");
              return out;
          }, py::arg("r"), "cvr_complement -> Region");
    m.def("cvr_symm_difference", [](const CvrRegion& a, const CvrRegion& b,
                                    CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_symm_difference(a, b, w, h, out), "cvr_symm_difference");
              return out;
          }, py::arg("r1"), py::arg("r2"), py::arg("w"), py::arg("h"),
             "cvr_symm_difference -> Region");

    /* --- 连通域 --- */
    m.def("cvr_connection", [](const CvrRegion& r, int connectivity, CvrCoord w, CvrCoord h) {
              std::vector<CvrRegion> out;
              check(cvr::cvr_connection(r, connectivity, w, h, out), "cvr_connection");
              return out;
          }, py::arg("r"), py::arg("connectivity") = 8, py::arg("w"), py::arg("h"),
          "cvr_connection -> list[Region]");

    /* --- 生成 / 结构元 --- */
    m.def("cvr_gen_circle", [](CvrCoord row, CvrCoord col, double radius) {
              return cvr::cvr_gen_circle(row, col, radius);
          }, py::arg("row"), py::arg("col"), py::arg("radius"),
          "cvr_gen_circle -> Region（HALCON gen_circle）");
    m.def("cvr_gen_rectangle1", [](CvrCoord r1, CvrCoord c1, CvrCoord r2, CvrCoord c2) {
              return cvr::cvr_gen_rectangle1(r1, c1, r2, c2);
          }, py::arg("r1"), py::arg("c1"), py::arg("r2"), py::arg("c2"),
          "cvr_gen_rectangle1 -> Region（HALCON gen_rectangle1）");
    m.def("cvr_gen_rectangle2", [](double row, double col, double phi,
                                   double length1, double length2) {
              return cvr::cvr_gen_rectangle2(row, col, phi, length1, length2);
          }, py::arg("row"), py::arg("col"), py::arg("phi"),
             py::arg("length1"), py::arg("length2"),
             "cvr_gen_rectangle2 -> Region（HALCON gen_rectangle2）");
    m.def("cvr_se_disk", [](int radius) { return cvr::cvr_se_disk(radius); },
          py::arg("radius"), "cvr_se_disk -> Region（以 (0,0) 为心的结构元）");
    m.def("cvr_se_rect", [](int w, int h) { return cvr::cvr_se_rect(w, h); },
          py::arg("w"), py::arg("h"), "cvr_se_rect -> Region");
    m.def("cvr_se_cross", [](int radius) { return cvr::cvr_se_cross(radius); },
          py::arg("radius"), "cvr_se_cross -> Region");
    m.def("cvr_se_transpose", [](const CvrRegion& se) {
              CvrRegion out;
              check(cvr::cvr_se_transpose(se, out), "cvr_se_transpose");
              return out;
          }, py::arg("se"), "cvr_se_transpose -> Region");

    /* --- 形态学 --- */
    m.def("cvr_dilation1", [](const CvrRegion& r, const CvrRegion& se, int iterations,
                              CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_dilation1(r, se, iterations, w, h, out), "cvr_dilation1");
              return out;
          }, py::arg("r"), py::arg("se"), py::arg("iterations"), py::arg("w"), py::arg("h"),
          "cvr_dilation1 -> Region");
    m.def("cvr_erosion1", [](const CvrRegion& r, const CvrRegion& se, int iterations,
                             CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_erosion1(r, se, iterations, w, h, out), "cvr_erosion1");
              return out;
          }, py::arg("r"), py::arg("se"), py::arg("iterations"), py::arg("w"), py::arg("h"),
          "cvr_erosion1 -> Region");
    m.def("cvr_opening", [](const CvrRegion& r, const CvrRegion& se, CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_opening(r, se, w, h, out), "cvr_opening");
              return out;
          }, py::arg("r"), py::arg("se"), py::arg("w"), py::arg("h"), "cvr_opening -> Region");
    m.def("cvr_closing", [](const CvrRegion& r, const CvrRegion& se, CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_closing(r, se, w, h, out), "cvr_closing");
              return out;
          }, py::arg("r"), py::arg("se"), py::arg("w"), py::arg("h"), "cvr_closing -> Region");
    m.def("cvr_fill_up", [](const CvrRegion& r, CvrCoord w, CvrCoord h) {
              CvrRegion out;
              check(cvr::cvr_fill_up(r, w, h, out), "cvr_fill_up");
              return out;
          }, py::arg("r"), py::arg("w"), py::arg("h"), "cvr_fill_up -> Region");

    /* --- 形状特征 --- */
    m.def("cvr_feature_area_center", [](const CvrRegion& r) {
              double row = 0, col = 0;
              CvrChords area = 0;
              check(cvr::cvr_feature_area_center(r, row, col, area), "cvr_feature_area_center");
              return py::make_tuple(row, col, static_cast<py::ssize_t>(area));
          }, py::arg("r"), "cvr_feature_area_center -> (row, col, area)");
    m.def("cvr_feature_moments", [](const CvrRegion& r) {
              double m11 = 0, m20 = 0, m02 = 0;
              check(cvr::cvr_feature_moments(r, m11, m20, m02), "cvr_feature_moments");
              return py::make_tuple(m11, m20, m02);
          }, py::arg("r"), "cvr_feature_moments -> (m11, m20, m02)");
    m.def("cvr_feature_elliptic_axis", [](const CvrRegion& r) {
              double ra = 0, rb = 0, phi = 0;
              check(cvr::cvr_feature_elliptic_axis(r, ra, rb, phi),
                    "cvr_feature_elliptic_axis");
              return py::make_tuple(ra, rb, phi);
          }, py::arg("r"), "cvr_feature_elliptic_axis -> (ra, rb, phi)");
    m.def("cvr_feature_contlength", [](const CvrRegion& r) {
              double v = 0;
              check(cvr::cvr_feature_contlength(r, v), "cvr_feature_contlength");
              return v;
          }, py::arg("r"), "cvr_feature_contlength -> float");
    m.def("cvr_feature_convexity", [](const CvrRegion& r) {
              double v = 0;
              bool is_convex = false;
              check(cvr::cvr_feature_convexity(r, v, is_convex), "cvr_feature_convexity");
              return py::make_tuple(v, is_convex);
          }, py::arg("r"), "cvr_feature_convexity -> (convexity, is_convex)");
    m.def("cvr_feature_smallest_rectangle1", [](const CvrRegion& r) {
              CvrCoord r1 = 0, c1 = 0, r2 = 0, c2 = 0;
              check(cvr::cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2),
                    "cvr_feature_smallest_rectangle1");
              return py::make_tuple(r1, c1, r2, c2);
          }, py::arg("r"), "cvr_feature_smallest_rectangle1 -> (row1, col1, row2, col2)");
    m.def("cvr_feature_smallest_rectangle2", [](const CvrRegion& r) {
              double row = 0, col = 0, phi = 0, l1 = 0, l2 = 0;
              check(cvr::cvr_feature_smallest_rectangle2(r, row, col, phi, l1, l2),
                    "cvr_feature_smallest_rectangle2");
              return py::make_tuple(row, col, phi, l1, l2);
          }, py::arg("r"),
          "cvr_feature_smallest_rectangle2 -> (row, col, phi, length1, length2)");
    m.def("cvr_feature_smallest_circle", [](const CvrRegion& r) {
              double row = 0, col = 0, radius = 0;
              check(cvr::cvr_feature_smallest_circle(r, row, col, radius),
                    "cvr_feature_smallest_circle");
              return py::make_tuple(row, col, radius);
          }, py::arg("r"), "cvr_feature_smallest_circle -> (row, col, radius)");
    m.def("cvr_feature_compactness", [](const CvrRegion& r) {
              double v = 0;
              check(cvr::cvr_feature_compactness(r, v), "cvr_feature_compactness");
              return v;
          }, py::arg("r"), "cvr_feature_compactness -> float");
    m.def("cvr_feature_circularity", [](const CvrRegion& r) {
              double v = 0;
              check(cvr::cvr_feature_circularity(r, v), "cvr_feature_circularity");
              return v;
          }, py::arg("r"), "cvr_feature_circularity -> float");
    m.def("cvr_feature_rectangularity", [](const CvrRegion& r) {
              double v = 0;
              check(cvr::cvr_feature_rectangularity(r, v), "cvr_feature_rectangularity");
              return v;
          }, py::arg("r"), "cvr_feature_rectangularity -> float");
    m.def("cvr_get_feature", [](const CvrRegion& r, const std::string& name) {
              double v = 0;
              check(cvr::cvr_get_feature(r, name, v), "cvr_get_feature");
              return v;
          }, py::arg("r"), py::arg("name"), "cvr_get_feature -> float");
    m.def("cvr_region_features", [](const std::vector<CvrRegion>& regions,
                                    const std::vector<std::string>& names) {
              std::vector<double> values;
              check(cvr::cvr_region_features(regions, names, values), "cvr_region_features");
              py::list out;
              const size_t ncol = names.size();
              if (ncol > 0) {
                  for (size_t i = 0; i + ncol <= values.size(); i += ncol) {
                      py::list row;
                      for (size_t k = 0; k < ncol; ++k) row.append(values[i + k]);
                      out.append(row);
                  }
              }
              return out;
          }, py::arg("regions"), py::arg("names"),
          "cvr_region_features -> list[list[float]]（N x M，行序同输入）");
    m.def("cvr_feature_mask_name", [](const std::string& name) {
              uint32_t bits = 0;
              check(cvr::cvr_feature_mask_name(name, bits), "cvr_feature_mask_name");
              return bits;
          }, py::arg("name"), "cvr_feature_mask_name -> int（单个名字 -> 位）");
    m.def("cvr_feature_mask_parse", [](const std::vector<std::string>& names) {
              uint32_t mask = 0;
              check(cvr::cvr_feature_mask_parse(names, mask), "cvr_feature_mask_parse");
              return mask;
          }, py::arg("names"),
          "cvr_feature_mask_parse -> int（名字列表 -> 掩码；none/all/basic/hull 也可）");
    m.def("cvr_region_precompute_features", [](const CvrRegion& r, uint32_t mask) {
              return cvr::cvr_region_precompute_features(r, mask);
          }, py::arg("r"), py::arg("mask"),
          "cvr_region_precompute_features -> 本次新置位的位数");

    /* --- 灰度特征 --- */
    m.def("cvr_gray_feature", [](const CvrRegion& r, const py::object& gray, int width,
                                 int height, const py::object& depth,
                                 const std::string& name) {
              CvrGrayDepth d = parse_depth(depth);
              int w = width, h = height;
              py::buffer_info info = gray_buffer(gray, d, w, h, false);
              double v = 0;
              check(cvr::cvr_gray_feature(r, info.ptr, w, h, d, name, v), "cvr_gray_feature");
              return v;
          }, py::arg("r"), py::arg("gray"), py::arg("width"), py::arg("height"),
             py::arg("depth"), py::arg("name"),
          "cvr_gray_feature -> float（name: area/row/column/mean/deviation/min/max/median）");
    m.def("cvr_gray_features", [](const std::vector<CvrRegion>& regions,
                                  const py::object& gray, int width, int height,
                                  const py::object& depth,
                                  const std::vector<std::string>& names) {
              CvrGrayDepth d = parse_depth(depth);
              int w = width, h = height;
              py::buffer_info info = gray_buffer(gray, d, w, h, false);
              std::vector<double> values;
              check(cvr::cvr_gray_features(regions, info.ptr, w, h, d, names, values),
                    "cvr_gray_features");
              py::list out;
              const size_t ncol = names.size();
              if (ncol > 0) {
                  for (size_t i = 0; i + ncol <= values.size(); i += ncol) {
                      py::list row;
                      for (size_t k = 0; k < ncol; ++k) row.append(values[i + k]);
                      out.append(row);
                  }
              }
              return out;
          }, py::arg("regions"), py::arg("gray"), py::arg("width"), py::arg("height"),
             py::arg("depth"), py::arg("names"),
          "cvr_gray_features -> list[list[float]]（N x M）");

    /* --- 图像 <-> region --- */
    m.def("cvr_bin_to_region", [](const py::object& gray, int width, int height,
                                  const py::object& depth, double threshold) {
              CvrGrayDepth d = parse_depth(depth);
              int w = width, h = height;
              py::buffer_info info = gray_buffer(gray, d, w, h, false);
              CvrRegion out;
              check(cvr::cvr_bin_to_region(info.ptr, w, h, d, threshold, out),
                    "cvr_bin_to_region");
              return out;
          }, py::arg("gray"), py::arg("width"), py::arg("height"), py::arg("depth"),
             py::arg("threshold"),
          "cvr_bin_to_region -> Region（gray >= threshold 为前景）");
    m.def("cvr_region_to_bin", [](const CvrRegion& r, CvrCoord w, CvrCoord h,
                                  const py::object& gray, const py::object& depth,
                                  double foreground, double background) {
              CvrGrayDepth d = parse_depth(depth);
              int ww = w, hh = h;
              py::buffer_info info = gray_buffer(gray, d, ww, hh, true);
              check(cvr::cvr_region_to_bin(r, ww, hh, info.ptr, d, foreground, background),
                    "cvr_region_to_bin");
          }, py::arg("r"), py::arg("w"), py::arg("h"), py::arg("gray"), py::arg("depth"),
             py::arg("foreground"), py::arg("background"),
          "cvr_region_to_bin：写入调用方提供的可写缓冲（区内 fg、区外 bg）");

    /* --- 形状变换 / 筛选 --- */
    m.def("cvr_shape_trans", [](const CvrRegion& r, const std::string& shape) {
              CvrRegion out;
              check(cvr::cvr_shape_trans(r, shape, out), "cvr_shape_trans");
              return out;
          }, py::arg("r"), py::arg("shape"), "cvr_shape_trans -> Region");
    m.def("cvr_select_shape", [](const std::vector<CvrRegion>& regions,
                                 const std::vector<std::string>& features,
                                 const std::string& op,
                                 const std::vector<double>& mins,
                                 const std::vector<double>& maxs) {
              std::vector<CvrRegion> selected;
              check(cvr::cvr_select_shape(regions, features, op, mins, maxs, selected),
                    "cvr_select_shape");
              return selected;
          }, py::arg("regions"), py::arg("features"), py::arg("op"),
             py::arg("mins"), py::arg("maxs"),
          "cvr_select_shape -> list[Region]（op: 'and' / 'or'）");
    m.def("cvr_select_shape_single", [](const std::vector<CvrRegion>& regions,
                                        const std::string& feature,
                                        double min_val, double max_val) {
              std::vector<CvrRegion> selected;
              check(cvr::cvr_select_shape_single(regions, feature, min_val, max_val,
                                                 selected),
                    "cvr_select_shape_single");
              return selected;
          }, py::arg("regions"), py::arg("feature"), py::arg("min_val"), py::arg("max_val"),
          "cvr_select_shape_single -> list[Region]");
}
