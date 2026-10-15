# cv-region

[HALCON](https://www.mvtec.com/products/halcon) 语义的游程（RLE）region 库 **`cvr_core`** 的 Python 绑定。

* 纯 C++17 / 纯 STL、**零三方依赖**、**与 HALCON 完全无关**（不需要 HALCON 安装，也不需要 license）；
* 实现只有「一个头 + 一个实现」：`include/cvr/cvr.hpp` + `src/cvr.cpp`，本包把它编成扩展模块；
* 集合运算 / 连通域 / 形态学 / 形状特征 / `select_shape` / 灰度特征 / region↔图像 互转；
* 与 numpy 互转走 buffer 协议，**核心功能不依赖 numpy**（没装 numpy 也能用）。

```python
import numpy as np
import cv_region as cvr
from cv_region import Region

mask = np.zeros((200, 300), np.uint8)
mask[20:80, 30:120] = 255
mask[120:180, 200:280] = 255

regions = Region.from_mask(mask).connection(8, 300, 200)      # 拆连通域
big = cvr.cvr_select_shape(regions, ["area", "circularity"], "and",
                           [500.0, 0.1], [1e30, 1.0])          # 按特征筛选

for r in big:
    row, col, area = r.area_center()
    print(row, col, area, r.get_feature("contlength"))

out = big[0].to_mask(300, 200)                                # 回到 numpy 掩码
```

## 安装

```bash
pip install cv-region          # 若已发布到 PyPI
pip install ./cv_region/python  # 从源码构建（本仓库内）
pip wheel  ./cv_region/python -w dist
```

需要一台带 C++17 编译器的机器（MSVC / GCC / Clang）+ CMake ≥ 3.21；
构建依赖 `scikit-build-core` 与 `pybind11` 由 pip 自动准备。

## API 结构

三层，命名规则固定：

1. **模块级函数保持 C++ 原名**，与 `cvr.hpp` 逐条对照；C++ 的出参在 Python 里变成返回值：

   | C++ | Python |
   |---|---|
   | `cvr_union2(r1, r2, w, h, out)` | `cvr_union2(r1, r2, w, h) -> Region` |
   | `cvr_connection(r, conn, w, h, out)` | `cvr_connection(r, conn, w, h) -> list[Region]` |
   | `cvr_feature_area_center(r, row, col, area)` | `cvr_feature_area_center(r) -> (row, col, area)` |
   | `cvr_select_shape(regions, feats, op, mins, maxs, out)` | `cvr_select_shape(...) -> list[Region]` |
   | `cvr_bin_to_region(gray, w, h, depth, thr, out)` | `cvr_bin_to_region(gray, w, h, depth, thr) -> Region` |

   全表：`cvr_union1/union2/intersection/difference/symm_difference/complement`、
   `cvr_connection`、`cvr_gen_circle/gen_rectangle1/gen_rectangle2`、
   `cvr_se_disk/se_rect/se_cross/se_transpose`、
   `cvr_dilation1/erosion1/opening/closing/fill_up`、`cvr_feature_*`、`cvr_get_feature`、
   `cvr_region_features`、`cvr_feature_mask_name/parse`、`cvr_region_precompute_features`、
   `cvr_gray_feature/gray_features`、`cvr_bin_to_region/region_to_bin`、
   `cvr_shape_trans`、`cvr_shape_trans_ex`、`cvr_select_shape/select_shape_single`、`cvr_last_error`。

2. **`Region` 方法**：方法名 = 函数名去掉 `cvr_` 以及紧随的 `region_` / `feature_` 前缀
   （它们已经是 `Region` 的方法了）：

   `normalize()`、`invalidate()`、`is_empty(w,h)`、`materialize(w,h)`、`bbox()`、
   `row_ranges(row)`、`check_invariants()`、`union2(r2,w,h)`、`intersection(...)`、
   `difference(...)`、`symm_difference(...)`、`complement()`、`connection(conn,w,h)`、
   `se_transpose()`、`dilation1(se,iter,w,h)`、`erosion1(...)`、`opening(se,w,h)`、
   `closing(...)`、`fill_up(w,h)`、`area_center()`、`moments()`、`elliptic_axis()`、
   `contlength()`、`convexity()`、`smallest_rectangle1/2()`、`smallest_circle()`、
   `compactness()`、`circularity()`、`rectangularity()`、`get_feature(name)`、
   `precompute_features(mask)`、`gray_feature(...)`、`gray_features(...)`、
   `shape_trans(shape)`、`shape_trans_ex(standard, ...)`、`to_bin(w,h,gray,depth,fg,bg)`。

3. **Pythonic 便捷方法**（非 1:1，纯糖）：

   * `Region.from_mask(mask)` —— 非零即前景（0/1、0/255、`bool` 掩码都行）
   * `Region.from_image(img, threshold, depth=None)` —— `gray >= threshold` 为前景
   * `Region.to_mask(w, h)` —— numpy `uint8`，前景 255 / 背景 0
   * `Region.to_image(w, h, dtype="uint8", foreground=255, background=0)` —— 任意 dtype
   * `Region.to_bin_bytes(w, h, foreground=255, background=0)` —— 不需要 numpy

## 关于 `depth`

灰度缓冲的像素类型用 `CvrGrayDepth` 表示，三写法等价：

```python
Region.from_image(img, 128.0, "uint8")     # 字符串别名
Region.from_image(img, 128.0, cvr.CVR_GRAY_U8)   # 整数（与 C++ 同名常量）
Region.from_image(img, 128.0, GrayDepth.U8)      # 枚举
```

别名对照：`byte/u8/uint8` → `int1/s8` → `uint2/u16/uint16` → `int2/s16/int16`
→ `int4/s32/int32` → `int64/s64` → `real/f32/float32` → `double/f64/float64`。
（裸 `int8` 有歧义 —— HALCON 的 `int8` 是 64 位，numpy 的 `int8` 是 8 位 —— 所以明确拒绝，
请写 `int1` 或 `int64`。）

灰度缓冲可以是任意支持 **buffer 协议**的对象（numpy 数组、`memoryview`、
`bytearray`、`array.array`），但必须 **C 连续**，元素宽度要与 `depth` 一致。

## 错误处理

C++ 侧是「返回 `bool` + `cvr_last_error()` 取错误串」，在 Python 里统一变成异常：

```python
>>> Region([(0, 0, 1)]).get_feature("no_such_feature")
RuntimeError: cvr_get_feature: unknown feature name: no_such_feature
```

错误消息就是 `cvr_last_error()` 的内容（原始 C++ 文本，未加工）。

## 与 HALCON 的一致性（2026-10-09 更新）

**已对齐**（依据 HALCON Operator Reference：`contlength` 与 `gen_contour_region_xld`）：

| 特征 / 算子 | 口径 | 验证 |
|---|---|---|
| `contlength` | HALCON `'center'` 轮廓：轮廓点 = **边界像素中心**，沿轴 1 / 对角 √2，**孔洞不计**，空区域 0 | 51×81 实心矩形 = **260**（与 HALCON 实测一致）；8×8 = 28；转置不变 |
| `shape_trans("convex")` | 走与 HALCON 一致的多边形栅格器（像素方格 `[r,r+1)×[c,c+1)`，行取 `floor`） | `convex ⊇ 原区域`；轴对齐档逐像素一致 |

> **`shape_trans_ex`**（本库自定义，无 HALCON 对应）：用**标准形状模板**补全**有缺失的提取形状**
> （各自取最大外轮廓 → 弧长重采样 → 归一化 → Trimmed 相似 ICP → 环形缺失段检测 → 拼接 → 周期三次 B 样条平滑 → 栅格化）。
> 返回 `(Region, coverage)`，`coverage` = 模板轮廓被观测覆盖的比例。两个输入必须各恰好一个连通分量。
> 参数与 `cvr_shape_trans_ex` 的 `(n_samples, trim_ratio, dist_ratio, min_len, smooth_s)` 一一对应，
> 默认 `1000 / 0.65 / 0.04 / 20 / 8.0`；`smooth_s` 是残差平方和上限（对齐 scipy `splprep` 的 `s`）。
> 缺失比例过大（≳ 一半）时因归一化偏置会偏小（详见仓库根 `README.md` 的 `cv_shape_trans_ex` 小节）。

> `gen_contour_region_xld` 原文：“for diagonal border elements `'border'` uses two contour
> segments of length 1 each, whereas `'center'` uses a single segment of length √2” ——
> 即中心轮廓对对角相邻的边界像素**直接走一条 √2 对角边**（贴边跟踪），不绕直角。

**仍与 HALCON 不一致**（都是**公式/实现口径本身不同**，与上面这次修复无关）：

| 特征 | HALCON 的定义 | 本实现 |
|---|---|---|
| `circularity` | `F / (π · maxdist²)`（maxdist = 中心到轮廓像素的最大距离），裁剪到 ≤1 | `1 / max(1, contlength²/(4πF))`（**不同公式**） |
| `rectangularity` | 「与区域同阶矩的矩形」与区域的差集面积 ÷ 矩形面积 | `area / smallest_rectangle2 面积`（**不同定义**） |
| `convexity` | 凸包面积按**像素数** | 凸包**多边形面积**（略有偏差） |
| `compactness` | `max(1, contlength²/(4πF))` | 同式但**缺 `max(1,·)` 下限裁剪** |
| `phi` / `structure_factor` | 见各自算子页 | 未逐个核对 |

`area` / `row` / `column` / `width` / `height` / `row1`–`column2` / `ra` / `rb` /
`anisometry` / `bulkiness` 已与 HALCON 逐值一致；集合运算、`connection`、形态学、
`select_shape`（筛选逻辑本身）都正常。

> 顺带一提：`cv_region/tests/test_m1..m10.cpp` 里的关键数值全部用 `assert()` 写的，
> Release 构建下 `assert` 被 `NDEBUG` 编译掉 ⇒ 那些期望值**从未真正被执行**。
> 本包的 `tests/test_cv_region.py` 是同一批口径的**可执行**版本（已纠正其中几处，
> 并新增了 HALCON 口径的回归基线）。

## 与 HALCON 扩展包的关系

同一份 C++ 源码同时供两个消费方使用：

* **HALCON 扩展包** `Halcon_SoftwarePackage.dll` —— 里面的 `cv_*` 系列算子
  （`cv_bin_to_region` / `cv_connection_ex` / `cv_select_shape` …）直接链静态库 `cvr_core`；
* **本 PyPI 包** —— 同一个 `cvr.cpp` 编进扩展模块，脱离 HALCON 独立使用。

所以两边行为一致：`cvr_region_cache.hdev` 里能跑出来的结果，这里用 numpy 也能复现。

## 许可

GPL-3.0-or-later（与 `Halcon_SoftwarePackage` 仓库一致）。

## 开发

```bash
pip install -e ./cv_region/python   # 可编辑安装
pytest                              # 或 python -m pytest cv_region/python/tests
```

测试 `tests/test_cv_region.py` 里的关键数值（面积 / 重心 / 轮廓长度 / 凸度 / 矩形度 /
`select_shape` 结果）与 C++ 侧 `cv_region/tests/test_m1..m10` 用同一套期望值，
用来保证 Python 绑定与 C++ 实现逐值一致。
