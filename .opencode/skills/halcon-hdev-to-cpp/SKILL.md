---
name: halcon-hdev-to-cpp
description: '把 HALCON/HDevelop 过程（.hdev）翻译成纯 C++、不使用任何 HALCON 算子 的指南：列算子清单 → 先跑原脚本取基准 → 逐算子映射到 cvr 核心库/OpenCV → 坑清单 → 编译通过（数值对拍作为可选后续）。重点含 图像↔region 互转（cv_bin_to_region / cv_region_to_bin）在 C++ 里该怎么写（cvr 只有 OpenCV 门控的 0/1 mask 版，阈值版要自己补接口）、phi 角度约定（HALCON 与 OpenCV/cvr 反号、minAreaRect 的 180°/宽高交换等价）、cv_subtract 的 f32 输出不饱和、cv_shape_trans 元组语义、递归扫目录自吞输出等。当用户要求"把某个 .hdev 函数翻成 C++"、"这个 HALCON 算子在 C++ 里对应什么/怎么写"、"不用 HALCON 算子实现这段流程"时使用。'
---

# HALCON/HDevelop → 纯 C++ 翻译指南

> 目标读者：AI agent 或工程师，需要把一段 .hdev（HALCON 过程或脚本）**在没有 HALCON 的环境里**跑起来。
> 本项目里 `cvr`（`Halcon_SoftwarePackage/cv_region`，单头 `cvr.hpp` + 单 cpp）就是扩展包 `cv_*` 算子的**同一份底层实现**，
> 所以"翻译"多数时候不是重新发明，而是**换一层调用**。

## 0. 铁律（踩过的坑换来的）

1. **算法进静态库，翻译只做参数搬运**。翻译时发现 cvr 缺接口 ⇒ **先把接口补进 `cvr.hpp` + `cvr.cpp`**，再在业务工程里调用；**不要在业务工程里复制一份实现**（否则两份实现必然漂移）。
2. **当前阶段目标是「翻译 + 编译通过」**，不要求运行对拍：代码翻出来、`cmake --build` 0 error 跑通即可（§5）。
   （可选后续：`hrun -d xx.hdev` 取基准、两边逐值比 —— 没有基准的"翻译正确"只是自我感觉，进阶时再做。）
3. **不要相信 README 的散文，要看供给层代码**：参数的真实语义（是否直通 OpenCV、是否有哨兵值、裁剪/饱和行为）只有供给层能回答。
4. （做对拍时）角度、区域这类有"等价表示"的量要**比几何**（方向向量内积、逐 run 比较），不要比原始数值。
5. **如实翻译，不要自作聪明**：hdev 里一步就翻一步。**不要做代数等价改写**（如把 `cv_subtract(A,B,'f32')` + `binary_inv` 合并成一步反向减法）、
   不要顺手改参数/顺序/中间产物。确实必须改写时（例如接口缺失），在注释里写清「原链是什么、为什么改」。
   注意：`cv_*` 算子是对 OpenCV 的**薄封装**，所以「照着算子链翻成等价的 OpenCV 调用」本身就是如实翻译，不需要再化简。

## 1. 流程

| 步 | 做什么 | 产出 |
|---|---|---|
| 1 | 读 .hdev（是 XML：`<procedure>` + `<l>` 行），列出**全部算子调用** | 算子清单（标注：本包 `cv_*` / HALCON 原生 / 纯控制流） |
| 2 | 逐个算子查**供给层**（`source/Halcon_*.cpp`）确认真实语义与默认值 | 映射表（见 §2） |
| 3 | 写 C++：图像类 → OpenCV，region 类 → `cvr::`，控制流照抄 | 单个 .cpp + 极简 CMakeLists |
| 4 | **保证编译通过**：`cmake --build build --config Release` 0 error + 静态自查（§5） | 可运行的 exe |

读 .hdev 的注意：它是 **XML**（`<hdevelop><procedure name="main"><body><l>…</l></body></procedure></hdevelop>`），
文本里 `<` 写 `&lt;`；`.hdev` 含中文请用 UTF-8 **带 BOM**（无 BOM 时 MSVC 按 GBK 解码会报一堆莫名错误，见 §4）。

## 2. 算子映射速查

| hdev 里的调用 | C++ 里怎么写 |
|---|---|
| `cv_blur(Image, Out, Kwidth, Kheight, AnchorX, AnchorY, BorderType)` | `cv::blur(src, dst, cv::Size(kw, kh), cv::Point(-1,-1), 4)`（`-1`=核中心，`4`=BORDER_DEFAULT=OpenCV 默认） |
| `cv_subtract(A, B, Out, 'f32')` | `cv::subtract(A, B, dst, cv::noArray(), CV_32F)` —— **输出 real 不饱和、可为负**，别用 `saturate_cast<uchar>` |
| `cv_add / multiply / divide` | 同上对应 `cv::add/…`，`dtype` 参数直通 OpenCV |
| `cv_threshold(Image, Out, Thresh, MaxVal, Type, ThreshUsed)` | `cv::threshold(src, dst, Thresh, MaxVal, cv::THRESH_*)`；**`Thresh` 是字面值**（只有 `otsu`/`triangle` 标志才特殊，且要求 byte 输入）；`Type` 支持 `'binary'/'binary_inv'/…` 及 `'binary\|otsu'` 组合 |
| `cv_bin_to_region(Image, Region, Threshold)` | **见 §3**（`gray >= Threshold`，任意数值类型；cvr 里没有现成的阈值版接口，要按 §3 补/写） |
| `cv_region_to_bin(Region, Image, ForegroundGray, BackgroundGray, Width, Height)` | **见 §3**（写 fg/bg 到任意数值类型缓冲） |
| `cv_connection(Region, Conn)` | `cvr::cvr_connection(region, 8, w, h, conns)`（HALCON 默认 8 邻域） |
| `cv_region_features(Regions, Features, Values)` | `cvr::cvr_region_features(regions, names, values)`（行主序 [N×F]） |
| `cv_select_shape(Regions, Sel, Feat, 'and', Min, Max)` | `cvr::cvr_select_shape(regions, {feat}, "and", {min}, {max}, sel)`（`min` 可用本批最大特征值） |
| `cv_shape_trans(Regions, Out, 'rectangle2')` | 算子层是"元组→单 region"：C++ 里 **逐元素 `cvr::cvr_shape_trans` 再 `cvr_union2` 求并** |
| `cv_smallest_rectangle2 / cv_elliptic_axis` | `cvr::cvr_feature_smallest_rectangle2 / cvr_feature_elliptic_axis`；**phi 用 cvr 原始口径 = OpenCV 口径**（cv 层不取反，见 §4.1） |
| `cv_gen_rectangle2` | `cvr::cvr_gen_rectangle2(row, col, phi_cv, l1, l2)`（**phi 直接给 cv/OpenCV 口径**；HALCON 口径的换算只属于封装算子的那一层，翻译时不考虑） |
| `gray_features(Gray, Img, Features, Value)` | `cvr::cvr_gray_features(regions, gray_ptr, w, h, depth, names, values)`（8 个特征子集，与 HALCON 四类型实测一致） |
| `get_image_size` | `img.cols / img.rows`（OpenCV） |
| `read_image` | `cv::imread(path, cv::IMREAD_GRAYSCALE)` |
| `list_files(..., 'recursive')` | `std::filesystem::recursive_directory_iterator` —— **注意排除输出目录**（见 §4） |

## 3. 图像 ↔ region 互转在 C++ 里怎么写（重点）

### 3.1 先认清现状：cvr 里"有什么、缺什么"

| 接口 | 位置 | 可用性 | 语义 |
|---|---|---|---|
| `cvr_region_to_mask(const CvrRegion&, w, h, cv::Mat&)` | `cvr.hpp`/`cvr.cpp` 的 **`#ifdef CVR_WITH_OPENCV` 段** | 只有定义该宏才编出来 | region → **0/1 mask** |
| `cvr_region_from_mask(const cv::Mat&, CvrRegion&, w*, h*)` | 同上 | 同上 | **非零 mask** → region |
| `cvr_region_to_mask(cvr_region_h, w, h, uint8_t*)` 等 | 文件末尾**遗留 C ABI** 段 | 无人调用、仅 uint8 | 裸缓冲 |

⇒ **缺的正是 `cv_bin_to_region` / `cv_region_to_bin` 的本体**（任意数值类型 + **阈值** / **前景背景值**），
它现在只存在于扩展包供给层（`source/Halcon_CVRegion.cpp` 的 `buf_to_region_impl` / `image_to_region` / `Hcv_region_to_bin`）。
**不要**用 `cvr_region_from_mask` 去顶替：它只吃 0/1 mask、还要 OpenCV。

### 3.2 正解：先把接口补进 cvr（推荐，一次到位）

```cpp
// cvr.hpp（放在非门控段；CvrGrayDepth 复用灰度特征那套：U8/S8/U16/S16/S32/S64/F32/F64）
bool cvr_bin_to_region(const void* gray, int width, int height, CvrGrayDepth depth,
                       double threshold, CvrRegion& out);          // gray >= threshold 为前景

bool cvr_region_to_bin(const CvrRegion& r, CvrCoord w, CvrCoord h,
                       void* gray, CvrGrayDepth depth,
                       double foreground, double background);      // 区域内 fg / 区域外 bg
```
> 位置：`cvr.hpp` 非门控段（`cvr_bin_to_region` 约 656 行、`cvr_region_to_bin` 约 659 行；实现在 `cvr.cpp` 的 `cvr_gray_features` 之前）。
> **已随本次落地并通过整包构建 + 单测 9/9**（尚未与 HALCON 算子逐值对拍）。

实现要点：region→图像时**先用 `cvr_region_materialize` 处理 `is_compl`**（输出到临时对象，别原地别名），
图像→region 末尾调 `cvr_region_normalize` 保证 `(r,cb)` 有序；两侧都按 `[0,w)×[0,h)` 裁剪。
**遗留**：供给层的 `cv_bin_to_region`/`cv_region_to_bin` 仍用自己那份 `buf_to_region_impl`/`image_to_region`
（语义一致、实现两份），后续应瘦身为直接调上面的 cvr 接口。

### 3.3 备用写法（若某个分支里接口不可用时，可直接抄）

```cpp
// 图像 -> region：等价 cv_bin_to_region(Image, Region, Threshold)
template <class T>
bool bin_to_region_impl(const T* px, int32_t w, int32_t h, double threshold, CvrRegion& out)
{
    out.runs.clear(); out.is_compl = false;
    for (int32_t r = 0; r < h; ++r) {
        const T* line = px + (size_t)r * (size_t)w;
        int32_t cb = -1;
        for (int32_t c = 0; c < w; ++c) {
            if ((double)line[c] >= threshold) { if (cb < 0) cb = c; }
            else if (cb >= 0) { out.runs.push_back({r, cb, c - 1}); cb = -1; }
        }
        if (cb >= 0) out.runs.push_back({r, cb, w - 1});
    }
    return cvr::cvr_region_normalize(out);      // 必须有序，后续算子/特征都依赖
}

// region -> 图像：等价 cv_region_to_bin(Region : BinImage : fg, bg, w, h)
template <class T>
bool region_to_bin_impl(const CvrRegion& r, int32_t w, int32_t h,
                        T* px, double fg, double bg)
{
    if (!px || w <= 0 || h <= 0) return false;
    for (size_t i = 0; i < (size_t)w * (size_t)h; ++i) px[i] = (T)bg;
    CvrRegion tmp; const CvrRegion* src = &r;
    if (r.is_compl) { if (!cvr::cvr_region_materialize(r, w, h, tmp)) return false; src = &tmp; }
    for (size_t k = 0; k < src->runs.size(); ++k) {
        const CvrRun& rr = src->runs[k];
        if (rr.r < 0 || rr.r >= h) continue;
        T* line = px + (size_t)rr.r * (size_t)w;
        const int32_t cb = rr.cb < 0 ? 0 : rr.cb;
        const int32_t ce = rr.ce >= w ? w - 1 : rr.ce;
        for (int32_t c = cb; c <= ce; ++c) line[c] = (T)fg;
    }
    return true;
}
```
按类型分派（`switch (depth)` → `bin_to_region_impl<uint8_t/int8_t/uint16_t/int16_t/int32_t/int64_t/float/double>`）。

### 3.4 如果手上已经是 `cv::Mat`

```cpp
// Mat -> region：Mat 连续时直接把 data 交给阈值版
CV_Assert(m.isContinuous());
cvr::CvrRegion reg;
cvr::cvr_bin_to_region(m.data, m.cols, m.rows,
                        m.type() == CV_8UC1 ? cvr::CVR_GRAY_U8 : cvr::CVR_GRAY_F32,
                        /*threshold=*/1.0, reg);

// region -> Mat：先建好同尺寸同类型的 Mat 再写
cv::Mat bin(h, w, CV_8UC1);
cvr::cvr_region_to_bin(reg, w, h, bin.data, cvr::CVR_GRAY_U8, 255.0, 0.0);
```
若要"0/1 mask"语义且已开 `CVR_WITH_OPENCV`，也可直接用 `cvr_region_to_mask` / `cvr_region_from_mask`（它们内部就是这个语义）。

### 3.5 常见误用

- 用 `cvr_region_from_mask` 顶替 `cv_bin_to_region`：阈值 <1 或非 0/1 值时会错（mask 只看非零），且强依赖 OpenCV。
- 忘了 `cvr_region_normalize`：`runs` 乱序 ⇒ 后续特征/形态学结果错（很多算子默认输入有序）。
- `region→图像` 忘了处理 `is_compl`（补集）⇒ 输出整片背景。
- 越界不裁剪：region 坐标可能超出图像（负行、超出 w/h），必须裁剪，否则越界写。

## 4. 坑清单

1. **角度约定**：HALCON 的 `phi` 与 cvr/OpenCV **反号**（实测：cvr 与 `cv::minAreaRect` 在正角上逐位相同；HALCON 相反）。
   - **翻译时一律用 cv/OpenCV 口径**（`cvr` 的原始 `phi`，不取反）：不要去照顾 HALCON 口径——那是封装算子那一层的事。
   - 画图（`cv::RotatedRect`）要用 **OpenCV 口径**；若拿到的是 HALCON 口径，画之前取反。
   - 比角度别直接比数值：主轴**无向**，`(L1,L2,θ) ≡ (L2,L1,θ+90°)`，`minAreaRect` 还会把角归一到 `[0,180)` ⇒ 用**方向向量内积 ≈ ±1** 判等。
2. **`'f32'` 不饱和**：`cv_subtract(A,B,out,'f32')` 结果是 real、可为负，后续阈值就是拿负值判的（如 `binary_inv` + `Thresh=-1`），**不要**在中间转成 8 位。
3. **`cv_threshold` 的 `Thresh` 是字面值**，不是"自动阈值哨兵"；`otsu`/`triangle` 才自动，且**要求 byte 图**。
4. **`cv_shape_trans` 元组语义**：算子对"region 元组"输出单个 region，C++ 里要逐元素转换后 `cvr_union2` 求并。
5. **递归扫目录会自吞输出**：原脚本常是 `list_files(…, 'recursive')`；若输出目录在输入目录内，下一轮会把自己的产物再喂回流程 ⇒ 扫描时排除输出目录，或输出到外部目录。
6. **无 BOM 的中文源文件**：MSVC 按 GBK 解码会让"注释吞掉下一行"，报一堆莫名其妙的语法错（伴 `warning C4819`）⇒ 业务工程的 .cpp 也要 UTF-8 **with BOM**（或加 `/utf-8`）。
7. **`hrun` 的 .hdev 必须是 XML**，纯文本 HDevelop 代码会报 `Parser error: Start tag expected`。
8. **参数顺序**：HDevelop 线性顺序 = `输入对象, 输出对象, 输入控制, 输出控制`（写错即"无效行"被静默跳过，断言假性通过）。

## 5. 验收（当前阶段：**翻译 + 编译通过**）

**硬指标：编译通过**

```bat
cmake -B build -S . -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_MANIFEST_MODE=OFF ^
  -DVCPKG_INSTALLED_DIR=<pkg>/build/vcpkg_installed ^
  -DCMAKE_PREFIX_PATH=<pkg>/build/vcpkg_installed/x64-windows
cmake --build build --config Release     :: 要求 0 个 error C#### / error LNK####
```
> 固定用 `C:/cmake-3.30.5-windows-x86_64/bin/cmake.exe`：PATH 上的新版本 cmake 可能与既有 build 树不符，报 `No preprocessor test for "Renesas"`。

**静态自查（编译过后逐条看）**

- 算子覆盖：对照 §2 映射表，hdev 里每个算子都有对应调用（别漏算子、别漏输出）。
- 类型：`'f32'` 分支真的用 `CV_32F`；`cv_threshold` 的 `Thresh` 按字面值处理。
- 角度：输出口径明确（OpenCV 口径 / HALCON 口径），与画图处一致（§4.1）。
- 边界：region 越界裁剪、`is_compl` 物化、图像→region 后 `cvr_region_normalize`。
- 输出：落到 `output/`（**别放进输入目录**，否则递归扫描会自吞，§4.5）。

**（可选/后续）数值对拍**：`hrun -d` 跑原脚本取基准 → 同图跑翻译版 → 数值 `|a-b| <= 1e-9*max(1,|a|)`、region 逐 run 比较、角度比方向向量内积。不阻塞当前目标。

## 6. 参考（本项目内）

- 语义权威：`README.md`（每个 `cv_*` 算子的签名、参数、已知偏差）；**更权威的是供给层** `source/Halcon_CVRegion.cpp`、`source/Halcon_OpenCV.cpp`。
- 核心库：`cv_region/include/cvr/cvr.hpp` + `cv_region/src/cvr.cpp`（单头单 cpp，段落映射见文件头）。
- 对照装置范式：独立 C++ 工程 `add_subdirectory(cv_region)` + OpenCV + vcpkg 工具链；同一份 dump 程序编两遍（改动前/后）逐值对比。
