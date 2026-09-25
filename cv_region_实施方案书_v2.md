# cv_region 实施方案书（v2 · 供 Kimi Code 执行）

> 本文档取代《cv_region 设计文档 v1》。任何与 v1 冲突之处，以本文为准。
> 依据：MVTec《Extension Package Programmer's Manual》(HALCON 24.11) 逐条核对后的修正版。

---

## 0. 目标一句话

用 C++17 写一个面向过程的开源 region 库（chord/runlength 编码，语义对齐 HALCON），
同时提供 OpenCV 桥接，并能以标准 HALCON 扩展包形式嵌入 HALCON（HDevelop 直接可见、可调用）。

---

## 1. 硬性约束（先读这个，全部来自手册核对）

| # | 约束 | 来源 |
|---|------|------|
| R1 | `HIMGCOOR` 普通版 = `int16_t`，XL（`HC_LARGE_IMAGES`）= `int32_t`。L1 内部统一 `int32_t`；**零拷贝 memcpy 仅在 `sizeof(CvrRun)==sizeof(Hchord)` 时启用（编译期 if），否则逐字段转换** | 手册 3.1.1 |
| R2 | `Hrlregion` 字段名是 `is_compl`（不是 `compl`）；`num`/`num_max` 类型是 `HITEMCNT` | 手册 4.2 |
| R3 | chord 三条件：单弦限一行、弦不重叠、按行号升序。**从数据库读入可依赖此不变量；写回时不保证也行（HPutDRL 自动整理）** | 手册 4.2 |
| R4 | supply 过程必须 `extern "C"`（hcomp 生成的接口按 C 符号链接），实现文件用 `.cpp` | 手册 1.4/7.1 |
| R5 | supply 返回 `H_MSG_TRUE`；内部 action 过程返回 `H_MSG_OK` | 手册 3.3/3.7 |
| R6 | 自定义错误码必须 **> 10000**，且返回前调用 `HSetErrText()` 挂消息 | 手册 3.7.1 |
| R7 | `HAllocRLTmp` 默认只保证 `DEF_RL_LENGTH = 50000` 条弦；读 region 必须包 `H_ERR_WRRLN2` → `HIncrRL` 重试循环 | 手册 5.6.5 / 图 5.42 |
| R8 | `hcomp` 一次调用只能生成**一种**接口：`-H` 与 `-C` 必须分两次调用 | 手册 7.1.1 |
| R9 | 循环宏 `HAllReg/HAllSegm/HAllFilter/HAllFilter2` 只遍历**第 1 个**输入对象参数；多输入对象算子用 `HGetObjNum`+`HGetObj`+`HGetComp`+`HGetRL` 显式循环 | 手册 5.3/6.1 |
| R10 | 二元 region 算子是 **tuple 按位对应**（长度相等，或一方单元素广播），不是笛卡尔积；DEF 中用 `value_number` 断言 | HALCON 算子语义 |
| R11 | 有输入对象的算子，supply 开头第一句调用 `HCkNoObj(proc_handle)` | 手册 5.6.2 |
| R12 | DEF 若声明 `parallelization` 的 `method` 含 `split_tuple`，则**每个**输入控制参数必填 `costs_weight`、每个输出控制参数必填 `postprocessing`；v2 一律不显式声明 parallelization（缺省 = 可重入、本地、不自动并行），后续优化再加 | 手册 2.2.13 |
| R13 | region 不携带 width/height；补集运算依赖定义域。定义域来源：优先显式 `Width/Height` 控制参数，缺省回退 `HReadGV(proc_handle, HGWidth/HGHeight, ...)`；补集参与且无法获得定义域 → 返回 R6 自定义错误 | 手册 4.2/5.6.5 |

---

## 2. 仓库结构

```
cv_region/
├── CMakeLists.txt
├── README.md
├── LICENSE (MIT)
├── cmake/FindHALCON.cmake            # 可选，找不到时优雅降级
├── include/
│   ├── cvr/
│   │   ├── cvr_types.hpp             # CvrCoord/CvrFeatureFlags/常量
│   │   ├── cvr_region.hpp            # CvrRun/CvrRegion/构造/查询
│   │   ├── cvr_ops.hpp               # 集合代数
│   │   ├── cvr_conn.hpp              # connection
│   │   ├── cvr_morph.hpp             # 形态学+结构元
│   │   ├── cvr_feat.hpp              # 特征（惰性缓存）
│   │   ├── cvr_shape.hpp             # shape_trans
│   │   ├── cvr_select.hpp            # select_shape/select_gray
│   │   └── cvr_io.hpp                # OpenCV 桥接（CV 可选依赖）
├── src/                              # 与 include 一一对应
├── halcon_bridge/
│   ├── include/cvr_halcon_bridge.hpp
│   └── src/cvr_halcon_bridge.cpp     # 仅当找到 HALCON 时编译
├── halcon_pkg/
│   ├── def/cvr_region.def
│   ├── source/*.cpp                  # supply(CIPXxx)+action(IPXxx)，extern "C"
│   └── CMakeLists.txt                # hcomp 两次调用 + 库
├── tests/
│   ├── core/                         # 纯 gtest，不依赖 HALCON/CV
│   ├── vs_halcon/                    # 有 HALCON 才编译：与 HALCON 结果对照
│   └── vs_opencv/                    # 有 CV 才编译
└── examples/
```

依赖方向：`halcon_pkg → halcon_bridge → core`；`core` 只依赖 STL；`cvr_io` 只依赖 core + OpenCV。
任何一层不得反向 include。

---

## 3. L1 核心层规格

### 3.1 类型

```cpp
// cvr_types.hpp
namespace cvr {
using CvrCoord  = int32_t;            // 内部统一 32 位，与 HALCON 类型无关
using CvrChords = int64_t;            // 对齐 HITEMCNT
constexpr double CVR_INF_VAL = 1e30;
}
```

```cpp
// cvr_region.hpp
struct CvrRun { CvrCoord r, cb, ce; };   // 闭区间；cb<=ce；同行多弦须间隔>=1 像素

struct CvrFeature { /* 见 v1 §3.3，原样保留；flags 用 CvrFeatureFlags */ };

struct CvrRegion {
    std::vector<CvrRun> runs;
    bool                is_compl = false;
    CvrFeature          feature;         // 惰性缓存，flags==0 表示未计算
};
```

**删除 v1 中的 `static_assert(sizeof(CvrRun)==sizeof(Hchord))`**（见 R1），桥接层用 `if constexpr (sizeof(CvrRun)==sizeof(Hchord))` 选 memcpy 或逐字段。

### 3.2 不变量（核心层所有公开函数必须维持）

- `runs` 按 `(r, cb)` 字典序升序；同一行内弦不重叠、不相接（相接必须合并）。
- 修改 `runs` 后必须调用 `cvr_region_invalidate()` 清 feature.flags。
- `is_compl=true` 时 `runs` 表示"洞"，逻辑 region = 定义域 − runs。

### 3.3 模块 API（签名与 v1 一致，除以下变更）

- 所有涉及补集或边界的算子带 `(CvrCoord w, CvrCoord h)`；纯几何特征不带。
- `cvr_connection` 增加 `int connectivity`（4/8）。
- 特征名集合以 v1 §5.5 为准；`cvr_get_feature` 用字符串查表分发，未知名返回 `false` 并输出错误码（见 §6 错误约定）。
- OpenCV 桥接全部收进 `cvr_io.hpp`，用 `#ifdef CVR_WITH_OPENCV` 保护；`find_package(OpenCV QUIET)`，找不到则 cvr_io 不构建、示例降级。

### 3.4 补集访问

- 实现 `cvr_line_segments(r, row, w)` 与 `CvrLineCursor`（v1 §4.3 原样保留）。
- 集合代数/形态学/connection 内部一律走行视图或"物化补集"二选一：实现时可先物化（简单、先正确），留 TODO 优化为行视图。**正确性优先于性能**。

---

## 4. L2 HALCON 桥接层规格

```cpp
// cvr_halcon_bridge.hpp —— 所有函数 extern "C" 由调用方包，这里保持 C++ 签名
namespace cvr {

// 读取：带 HIncrRL 重试（R7）。返回 CvrRegion（is_compl 透传）。
CvrRegion cvr_from_halcon(Hproc_handle ph, Hkey region_key);   // 内部 HGetComp+HGetRL

// 写出：分配+填充+存库+挂到输出参数 par_num（R3：写回前不必 normalize）
Herror cvr_output_region(Hproc_handle ph, const CvrRegion& r, int par_num);

// 定义域获取（R13）
Herror cvr_get_domain(Hproc_handle ph, CvrCoord* w, CvrCoord* h,
                      bool* has_domain);   // 先查控制参数约定，再 HGWidth/HGHeight
}
```

实现要点：
- `cvr_from_halcon`：`HGetRL` 前用 `HAllocRLTmp`；捕获 `H_ERR_WRRLN2` → `HFreeUpToTmp` + `HIncrRL` + 重分配重试（抄手册图 5.42 结构）。
- `cvr_output_region`：`HNewRegion`（自动 HCkP、自动加入第 1 个输出对象参数）。**注意 HNewRegion 只对应第 1 个输出参数**，若某算子有多个输出对象参数，扩展为 `cvr_output_region(ph, r, par_num)` 内部用 `HCrObj`+`HPutDRL`+`HDefObj` 组合（手册 5.4），v2 只实现第 1 参数版本即可。
- 转换函数按 R1/R2 写，字段名 `is_compl`，计数转 `HITEMCNT`。

---

## 5. L3 HALCON 扩展包规格

### 5.1 算子清单（v2 打通 5 个）

| 算子 | 语义 | 说明 |
|---|---|---|
| `cvr_union2` | 按位并 | R10 对应；纯 region 无补集时不需定义域 |
| `cvr_intersection` | 按位交 | 同上 |
| `cvr_erosion1` | 腐蚀 | 需要定义域（边界） |
| `cvr_connection` | 连通域 | 输出 tuple；4/8 由参数定 |
| `cvr_select_shape` | 按形状特征筛选 | 输出为输入子集（HDupObj 语义：只增引用，手册 6.2.3） |

### 5.2 DEF 文件模板（以 cvr_union2 为例，严格遵守手册第 2 章）

```def
/* cvr_region.def —— 所有算子一个文件；非 ASCII 用 UTF-8 */
cvr_union2 <- CIPUnion2[Region1,Region2:RegionUnion::]

short.english
  Union of two regions per index (cv_region).;

module
  foundation;

chapter.english
  UserExtensions,CVRegion;

functionality
  region;

parameter
  Region1: input_object;
  sem_type: region;
  multivalue: optional;

parameter
  Region2: input_object;
  sem_type: region;
  multivalue: optional;

parameter
  RegionUnion: output_object;
  sem_type: region;
  multivalue: optional;

value_number
  RegionUnion == Region1 && (Region2 == Region1 || Region2 == 1);
```

- 不声明 `parallelization`（R12）。
- `parameter` 行内 `名称: 类别;` 同_line_书写；文本槽以分号结尾，文本内分号用 `\;`。
- 参数名禁止下划线（手册 2.2.1）；外部算子名全小写下划线。

### 5.3 Supply 过程模板（`.cpp`，extern "C"）

```cpp
// source/CIPUnion2.cpp
#include "Halcon.h"
#include "cvr_halcon_bridge.hpp"

extern "C" Herror CIPUnion2(Hproc_handle proc_handle)
{
    Hcpar w_par, h_par;
    CvrCoord w = 0, h = 0;
    bool has_domain = false;

    HCkNoObj(proc_handle);                       // R11：第一句

    if (cvr::cvr_get_domain(proc_handle, &w, &h, &has_domain) != H_MSG_OK)
        return H_ERR_WIPV1;                      // 配 HSetErrText

    INT4_8 n1 = 0, n2 = 0;
    HGetObjNum(proc_handle, 1, &n1);             // R9：显式循环
    HGetObjNum(proc_handle, 2, &n2);
    /* 按 value_number 已断言 n2==n1 或 n2==1 */

    for (INT4_8 i = 1; i <= n1; i++) {
        Hkey k1, k2;
        HGetObj(proc_handle, 1, i, &k1);
        HGetObj(proc_handle, 2, (n2 == 1) ? 1 : i, &k2);
        CvrRegion A = cvr::cvr_from_halcon(proc_handle, /*region_key_of*/ k1);
        CvrRegion B = cvr::cvr_from_halcon(proc_handle, k2);
        CvrRegion R;
        if (!cvr::cvr_union2(A, B, w, h, R)) {   // L1 返回 bool
            HSetErrText("cvr_union2 failed: ...");
            return (Herror)10001;                // R6
        }
        cvr::cvr_output_region(proc_handle, R, 1);
    }
    return H_MSG_TRUE;                           // R5
}
```

（取 region_key：`HGetComp(proc_handle, obj_key, REGION, &region_key)`，封装在 `cvr_from_halcon` 重载里。）

### 5.4 构建（CMake）

```cmake
# hcomp 必须分两次调用（R8）
add_custom_command(OUTPUT ${GEN}/HCvr.c
    COMMAND ${HALCONROOT}/bin/${HALCONARCH}/hcomp -u -H -pcvr ${DEF_FILE}
    WORKING_DIRECTORY ${GEN})
add_custom_command(OUTPUT ${GEN}/HCCvr.c ${GEN}/HCCvr.h
    COMMAND ${HALCONROOT}/bin/${HALCONARCH}/hcomp -u -C -pcvr ${DEF_FILE}
    WORKING_DIRECTORY ${GEN})

# 三个库（命名固定，不得改，手册 1.2）
add_library(cvr SHARED ${SUPPLY_SOURCES} ${GEN}/HCvr.c)          # 放 bin/$HALCONARCH
add_library(cvr_c SHARED ${GEN}/HCCvr.c)                          # 放 bin/$HALCONARCH
# XL 版本：-DHALCON_XL=1 再编一套（库名加 xl 后缀），手册 7.2.6
```

安装/激活：包路径加入 `HALCONEXTENSIONS`（Windows 分号 / Linux 冒号分隔），Windows 还需把 `bin/%HALCONARCH%` 加进 `PATH`（手册 1.3，**不要把 DLL 拷进系统目录**）；Linux 把 `lib/$HALCONARCH` 加进 `LD_LIBRARY_PATH`。

---

## 6. 错误与日志约定

- L1 纯函数：返回 `bool`，失败时把 `cvr_last_error()`（thread_local 字符串）置位；不抛异常。
- 桥接层/包层：HALCON 错误码风格。包内自定义错误码从 **10001** 起，每个码一张表，集中在 `cvr_errors.hpp`；返回前必须 `HSetErrText`（R6）。
- L1 禁止 printf/IO（手册 3.6）。

---

## 7. 测试方案

1. **core 单测（gtest，始终构建）**：空/满/补集 region 的各算子；不变量检查器（有序、不重叠、flags 一致）作为每个测试的固定断言。
2. **vs_opencv**：随机二值 mask ⇄ CvrRegion 往返；形态学结果与 OpenCV `erode/dilate`（自定义矩形/十字结构元）对照。
3. **vs_halcon（黄金标准，最重要）**：HDevelop 脚本对同一组随机 region 分别跑 HALCON 原生 `union2/intersection/erosion1/connection/select_shape` 与 cvr 算子，输出 region 用 `xor` 差集应为空；特征值逐位比对（1e-9）。随机种子固定，case ≥ 500。
4. **包级测试**：`hcomp` 生成无警告；HDevelop 加载包后 5 个算子可调用、help 可打开（`help` 目录生成 `-M` 选项）。

---

## 8. 里程碑与验收（Kimi Code 任务拆分）

| 里程碑 | 内容 | 验收标准 |
|---|---|---|
| M0 | 仓库脚手架：CMake 选项（`CVR_BUILD_HALCON`/`CVR_BUILD_OPENCV`/`CVR_BUILD_TESTS`）、LICENSE、CI | 空工程在 Win/Linux 配置通过 |
| M1 | L1 数据结构与不变量 + `cvr_region_normalize` + gtest | 不变量测试全绿 |
| M2 | 集合代数 + connection + 形态学（先物化补集，正确优先） | core 单测 + vs_opencv 绿 |
| M3 | 特征 + shape_trans | vs_halcon 特征比对 1e-9 |
| M4 | select_shape / select_gray | vs_halcon 绿 |
| M5 | L2 桥接层（R1/R2/R7/R13 全落实） | vs_halcon 桥接单测绿 |
| M6 | L3 扩展包：5 算子 DEF + supply + hcomp×2 + CMake + help(-M) | HDevelop 可见可跑；手册 R4/R8/R10/R11 逐条自查 |
| M7 | XL 版本构建 + 文档 + 性能基线（vs HALCON 原生算子耗时比） | XL 库命名/加载正确 |

**执行顺序：M0→M1→M2→M3→M4→M5→M6→M7，禁止跳步。每个里程碑结束跑全部已存在测试。**

---

## 9. 禁止事项清单（ review 红线）

1. 禁止在 `.c` 文件里写 supply 过程（必须 `.cpp` + `extern "C"`）。
2. 禁止 hcomp 单次调用混用 `-H`/`-C`/`-P`/`-N`。
3. 禁止用 `HAllReg` 遍历第 2 个输入对象参数。
4. 禁止二元算子笛卡尔积输出。
5. 禁止错误码 ≤ 10000 或不挂 `HSetErrText`。
6. 禁止在 supply 里用 `malloc/free`（一律 HAlloc 家族，手册 3.2）。
7. 禁止 L1 include 任何 HALCON/OpenCV 头文件（`cvr_io.hpp` 除外）。
8. 禁止改包名/库名（手册 1.3）。
