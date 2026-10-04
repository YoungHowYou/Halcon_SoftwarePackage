# Halcon_SoftwarePackage

HALCON Extension Package -- 为 [MVTec HALCON](https://www.mvtec.com/products/halcon) 机器视觉框架提供 **OpenCV 图像处理**、**Eigen / Armadillo 数学运算**、**SQLite3 数据库**、**Modbus 工业通信**、**spdlog 日志** 和 **MySQL 数据库** 扩展能力，在 HDevelop / HALCON 脚本中即可直接调用。

## 功能模块

### SQLite3 数据库

| 算子 | 说明 |
|------|------|
| `sqlite3_open` | 打开/创建数据库（支持 `:memory:` 内存模式） |
| `sqlite3_close` | 关闭数据库连接 |
| `sqlite3_exec` | 执行 SQL 语句（INSERT / UPDATE / DELETE 等） |
| `sqlite3_get_table` | 执行查询并返回结果表 |
| `sqlite3_loadOrSaveDb` | 内存数据库与文件之间的加载/保存 |

### Modbus 通信

**连接**

| 算子 | 说明 |
|------|------|
| `modbus_rtu_connect` | 串口 RTU 连接（可配置波特率、校验位、数据位、停止位、RS232/RS485） |
| `modbus_tcp_connect` | TCP/IP 以太网连接 |
| `modbus_set_slave_ID` | 设置从站 ID |
| `modbus_close` | 关闭连接 |

**读写操作**

| 算子 | 说明 |
|------|------|
| `modbus_read_bits` / `modbus_write_bit` | 线圈读写（单个） |
| `modbus_write_bits` | 线圈批量写入 |
| `modbus_read_inputbits` | 读取输入状态（只读） |
| `modbus_read_registers` / `modbus_write_register` | 保持寄存器读写 |
| `modbus_write_registers` | 寄存器批量写入 |
| `modbus_read_register_float` / `modbus_write_register_float` | 32-bit 浮点数读写（支持字节序配置） |
| `modbus_read_register_int` / `modbus_write_register_int` | 32/64-bit 整数读写 |
| `modbus_strerror` | 获取错误信息 |

### spdlog 日志

基于 [spdlog](https://github.com/gabime/spdlog) 高性能 C++ 日志库封装，支持多种日志输出方式和灵活的格式配置。所有记录器均采用 **异步模式**（后台线程池写入），日志调用立即返回，不影响主流程性能。

**初始化（可选）**

| 算子 | 说明 |
|------|------|
| `spdlog_init_thread_pool` | 初始化异步线程池（队列大小、线程数）。首次创建记录器时会自动以默认参数初始化，此算子用于自定义参数。 |

**创建日志记录器**

| 算子 | 说明 |
|------|------|
| `spdlog_basic_logger_mt` | 创建基本文件日志记录器（写入单个文件，可选追加/截断模式） |
| `spdlog_rotating_logger_mt` | 创建滚动文件日志记录器（文件达到指定大小后自动轮转，保留指定数量的历史文件） |
| `spdlog_daily_logger_mt` | 创建每日轮转日志记录器（每天在指定时刻自动创建新文件，旧文件添加日期后缀） |
| `spdlog_stdout_color_mt` | 创建彩色控制台日志记录器（不同级别以不同颜色显示） |
| `spdlog_get` | 根据名称获取已创建的日志记录器 |

**记录日志**

| 算子 | 级别 | 说明 |
|------|------|------|
| `spdlog_trace` | 0 - trace | 最详细的跟踪信息，用于深度调试 |
| `spdlog_debug` | 1 - debug | 调试诊断信息 |
| `spdlog_info` | 2 - info | 常规运行信息（默认级别） |
| `spdlog_warn` | 3 - warn | 警告，潜在问题 |
| `spdlog_err` | 4 - error | 运行时错误 |
| `spdlog_critical` | 5 - critical | 致命错误 |
| `spdlog_log` | 自定义 | 通过 level 参数指定任意级别 |

**配置与管理**

| 算子 | 说明 |
|------|------|
| `spdlog_set_level` | 设置最低输出级别（低于此级别的消息不记录，6=off 关闭所有输出） |
| `spdlog_set_pattern` | 设置日志格式（支持 `%Y %m %d %H %M %S %e %n %l %v %t` 等占位符） |
| `spdlog_flush` | 立即刷新缓冲区，确保日志写入文件 |
| `spdlog_flush_on` | 设置自动刷新触发级别（达到该级别的日志写入后自动刷新） |
| `spdlog_drop` | 从注册表中移除指定名称的记录器 |
| `spdlog_drop_all` | 移除所有已注册的记录器 |
| `spdlog_shutdown` | 关闭日志系统，刷新并释放所有资源 |

### MySQL 数据库

基于 Oracle MySQL C 客户端库（libmysql）封装，HDevelop 中可直接连接 MySQL 并执行 SQL。

| 算子 | 说明 |
|------|------|
| `mysql_real_connect` | 连接 MySQL 服务器（host、user、passwd、db、port 等） |
| `mysql_query` | 执行 SQL 语句（INSERT/UPDATE/DELETE/SELECT 等） |
| `mysql_store_result` | 获取 SELECT 查询结果集 |

### OpenCV 与数学算子

这两类算子的完整清单见下方同名章节：

- [OpenCV & Exiv2 扩展算子](#opencv--exiv2-扩展算子)
- [数学 / 矩阵扩展算子](#数学--矩阵扩展算子)

### 工具算子

| 算子 | 说明 |
|------|------|
| `string_to_image` | 将字符串编码到 HALCON 图像中 |
| `image_to_string` | 从图像中解码字符串 |

## OpenCV & Exiv2 扩展算子

本扩展包中所有 OpenCV 相关算子（定义于 `def/Halcon_OpenCV.def`，实现于 `source/Halcon_OpenCV.cpp`）。

- HALCON 版本：24.11
- 依赖：OpenCV 4.x、exiv2

### 参数全开放约定

- **图像/滤波/矩阵类**：以**控制参数**直接传递（如 `cv_blur` 的 `AnchorX, AnchorY, BorderType`，`cv_mat_mul` 的 `Alpha, Beta, Flags`），按位置传满全部参数。
- **特征检测/匹配/变换估计类**（`cv_orb_detect`、`cv_sift_detect`、`cv_akaze_detect`、`cv_bf_knn_match`、`cv_estimate_affine_partial2d`、`cv_estimate_rigid_2d`）：设置项通过 **dict 键**传入（`set_dict_tuple` 写入后传 `DictHandle`），未设键取默认；输入图像/描述子/点集也以 dict 对象传，结果写回同一 dict。
- **枚举/宏参数**：均可**字符串或整数**两种写法——如 `cv_blur` 的 `BorderType` 传 'border_default' 或 `4`、`cv_threshold` 的 `Type` 传 'binary|otsu'（"|" 组合）、`cv_morphology_ex` 的 `Op/Shape` 传 'open'/'ellipse'。字符串不区分大小写，内部映射为 OpenCV 常量；旧脚本的整数写法完全兼容。
- **`BorderType` 的字符串取值**（`cv_blur` / `cv_filter2d` / `cv_sep_filter2d` / `cv_sobel`，大小写不敏感，`border_` 前缀可省）：'constant'=0、'replicate'=1、'reflect'=2、'wrap'=3、'reflect101'=4、'default'=4、'transparent'=5、'isolated'=16；这些整数也可直接传。

### 图像类型说明

| HALCON 类型 | 值 | OpenCV 类型 | 说明 |
|---|---|---|---|
| `byte` | `BYTE_IMAGE` | `CV_8UC1` | 8 位无符号单通道 |
| `uint2` | `UINT2_IMAGE` | `CV_16UC1` | 16 位无符号单通道 |
| `real` | `FLOAT_IMAGE` | `CV_32FC1` | 32 位浮点单通道 |

---

### 滤波类

#### cv_blur

归一化盒式滤波（均值滤波），对应 `cv::blur`。

```
cv_blur(Image : ImageOut : Kwidth, Kheight, AnchorX, AnchorY, BorderType)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageOut | 图像 | 输出 | 与输入同类型同尺寸 |
| Kwidth | 整数 | 输入 | 滤波核宽度，≥1 的奇数，默认 3 |
| Kheight | 整数 | 输入 | 滤波核高度，≥1 的奇数，默认 3 |
| AnchorX | 整数 | 输入 | 锚点 X（-1=核中心），默认 -1 |
| AnchorY | 整数 | 输入 | 锚点 Y（-1=核中心），默认 -1 |
| BorderType | 字符串/整数 | 输入 | 边界模式：枚举字符串或整数（取值见上方「`BorderType` 的字符串取值」），默认 4=`BORDER_DEFAULT` |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 滤波核宽/高非法（非 ≥1 的奇数） |
| 30002 | 输入图像类型不支持 |
| 30003 | OpenCV 执行异常 |

---

#### cv_median_blur

中值滤波，对应 `cv::medianBlur`（仅支持方形核）。

```
cv_median_blur(Image : ImageOut : Ksize)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageOut | 图像 | 输出 | 与输入同类型同尺寸 |
| Ksize | 整数 | 输入 | 滤波核尺寸，≥3 的奇数，默认 3 |

> OpenCV 限制：16 位与浮点图仅支持 `Ksize = 3` 或 `5`；`byte` 图可更大。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | Ksize 非法（非 ≥3 的奇数） |
| 30002 | 16 位图 Ksize > 5 |
| 30003 | 浮点图 Ksize > 5 |
| 30004 | 输入图像类型不支持 |
| 30005 | OpenCV 执行异常 |

---

#### cv_filter2d

图像卷积，对应 `cv::filter2D`。Ddepth/BorderType 支持字符串枚举（'same'/'f32'、'border_default' 等）或整数；`Ddepth` 非 -1/'same' 时按映射后的类型分配输出图（`'u8'`/`'u16'`/`'f32'` → byte/uint2/real）。

```
cv_filter2d(Image, Kernel : ImageOut : Ddepth, AnchorX, AnchorY, Delta, BorderType)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| Kernel | 图像 | 输入 | 卷积核，必须为 `real` 单通道，任意宽高 |
| ImageOut | 图像 | 输出 | 与输入同尺寸；类型由 `Ddepth` 决定（默认同输入） |
| Ddepth | 字符串/整数 | 输入 | 输出深度：`'same'`/-1 保持输入类型，`'u8'`/`'u16'`/`'f32'` 分别输出 `byte`/`uint2`/`real` |
| AnchorX | 整数 | 输入 | 锚点 X（-1=核中心） |
| AnchorY | 整数 | 输入 | 锚点 Y（-1=核中心） |
| Delta | 实数 | 输入 | 结果叠加偏移量 |
| BorderType | 字符串/整数 | 输入 | 边界模式：枚举字符串或整数（取值见上方「`BorderType` 的字符串取值」），默认 4=`BORDER_DEFAULT` |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | Kernel 非 `real` 单通道 |
| 30002 | 输入图像类型不支持 |
| 30003 | OpenCV 执行异常 |
| 30006 | Ddepth 取值不支持（`'s16'`/`'s32'`/`'f64'` 无 HALCON 对应类型或 OpenCV 不支持） |

---

#### cv_gaussian_kernel

生成高斯核，**1:1 封装** `cv::getGaussianKernel`（参数名与语义跟 OpenCV 一致）：输出 **`real` 单通道图像（float32）** 的 **`ksize`×1 列向量**（元素和归一化为 1），可直接作 `cv_sep_filter2d` 的 `KernelX`/`KernelY`。

```
cv_gaussian_kernel(Kernel : : ksize, sigma :)
```

HDevelop 调用（**只有输出对象的算子，实参里输出对象在最前**）：

```
cv_gaussian_kernel (Kernel, 5, 1.0)      * 5×1 列向量
cv_sep_filter2d (Image, Kernel, Kernel, ImageOut, 'same', -1, -1, 0.0, 'border_default')
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Kernel | 图像 | 输出 | `real`（float32）单通道，`ksize`×1 列向量（与 `cv::getGaussianKernel` 返回值一致） |
| ksize | 整数 | 输入 | 核长度（>=1，建议奇数） |
| sigma | 实数 | 输入 | 标准差；<=0 时按 OpenCV 规则自动推算 |

- `sigma <= 0` 的推算规则与 OpenCV 完全一致：奇数 `ksize <= 7` 用 OpenCV 内建小核表（`ksize=3` → `[0.25,0.5,0.25]`、`ksize=5` → `[0.0625,0.25,0.375,0.25,0.0625]`），其余用 $\sigma = 0.3\left(\frac{ksize-1}{2}-1\right)+0.8$。核已归一化（元素和 = 1）。
- 输出为**全图域**、单通道 `real` 图像；HALCON 的 `real` 即 32 位浮点（`FLOAT_IMAGE` ↔ `CV_32FC1`），取值精度与 OpenCV `CV_32F` 一致。
- **不加形状参数**（本包约定：基础算子照原库接口封装，不自行扩展模式）：需要 1×`ksize` 行向量核时对输出调 `cv_reshape (Kernel, KernelRow, 1)`。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | ksize < 1 |
| 30002 | OpenCV 执行失败（`getGaussianKernel` 抛异常 / 输出核布局异常） |

例程：`examples/opencv/cv_gaussian_kernel.hdev`（列向量形状与已知值、小核表/公式两条 sigma 路径、与 `cv_sep_filter2d` 串联、`cv_reshape` 行核、错误路径）

---

#### cv_sep_filter2d

可分离线性滤波，**1:1 封装** `cv::sepFilter2D`：先按 `KernelX` 逐行卷积、再按 `KernelY` 逐列卷积。与「一个二维核 + `cv_filter2d`」结果等价，但每像素运算量是 $O(k_x+k_y)$ 而非 $O(k_x k_y)$。

```
cv_sep_filter2d(Image, KernelX, KernelY : ImageOut : Ddepth, AnchorX, AnchorY, Delta, BorderType :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| KernelX | 图像 | 输入 | 行方向一维核：`real` 单通道，1×N 或 N×1（可直接用 `cv_gaussian_kernel` 的输出） |
| KernelY | 图像 | 输入 | 列方向一维核：`real` 单通道，1×N 或 N×1（惯用法与 KernelX 传同一个核） |
| ImageOut | 图像 | 输出 | 与输入同尺寸；类型由 `Ddepth` 决定（默认同输入） |
| Ddepth | 字符串/整数 | 输入 | `'same'`/-1 保持输入类型；`'u8'`/`'u16'`/`'f32'` → `byte`/`uint2`/`real` |
| AnchorX / AnchorY | 整数 | 输入 | 锚点（-1=核中心） |
| Delta | 实数 | 输入 | 结果叠加偏移量 |
| BorderType | 字符串/整数 | 输入 | 边界模式：枚举字符串或整数（取值见上方「`BorderType` 的字符串取值」），默认 4=`BORDER_DEFAULT` |

- **惯用法（与 OpenCV 一致）**：两个参数传同一个核（如 `cv_gaussian_kernel` 的输出列向量）；传行向量/列向量结果一致（实测逐点差 < 1e-4）。
- 传了真正的二维核（宽高都 >1）报 30002，不会当一维核照用。
- 运算量：每像素从二维核的 $O(k^2)$ 降到 $O(k)$（$k$=核长度），核越大优势越明显。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 核不是 `real` 单通道 |
| 30002 | 核不是一维（KernelX / KernelY 须为 1×N 或 N×1） |
| 30003 | OpenCV 执行异常 |
| 30004 | 输入图像类型不支持 |
| 30005 | Ddepth / BorderType 无法识别 |
| 30006 | Ddepth 取值不支持（`'s16'`/`'s32'`/`'f64'`） |

例程：`examples/opencv/cv_sep_filter2d.hdev`（`cv_gaussian_kernel` 列向量核的 OpenCV 惯用法 + `cv_reshape` 行核、与 `cv_filter2d`（手工外积 2D 核）逐点对拍、anchor 全参数、byte/uint2/f32、错误路径）

---

#### cv_sobel

Sobel 边缘检测（一阶/二阶导数），对应 `cv::Sobel`。

```
cv_sobel(Image : ImageOut : Dx, Dy, Ksize, Ddepth, Scale, Delta, BorderType)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageOut | 图像 | 输出 | 统一输出 `real`（CV_32F，梯度含符号） |
| Dx | 整数 | 输入 | x 方向导数阶数（0~2），默认 1 |
| Dy | 整数 | 输入 | y 方向导数阶数（0~2），默认 0 |
| Ksize | 整数 | 输入 | 核尺寸（1 / 3 / 5 / 7），默认 3 |
| Ddepth | 字符串/整数 | 输入 | 输出深度：`'same'`(-1) / `'s16'`(3) / `'f32'`(5) / `'f64'`(6)，默认 -1（同输入，与 `cv::Sobel` 一致） |
| Scale | 实数 | 输入 | 缩放系数，默认 1.0 |
| Delta | 实数 | 输入 | 偏置，默认 0.0 |
| BorderType | 字符串/整数 | 输入 | 边界模式：枚举字符串（如 'replicate'）或整数（取值见上方「`BorderType` 的字符串取值」），默认 4=`BORDER_DEFAULT` |

> 输出统一为 `real`：Ddepth 计算后统一 `convertTo` CV_32F，因为 HALCON 无 64 位浮点图。
> ⚠️ Ddepth 默认 -1 表示中间缓冲与输入同深度（与 `cv::Sobel` 一致）：`byte` 输入时负梯度会被截断成 0，需要带符号精度请传 `'s16'`/`'f32'`/`'f64'`。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | Dx/Dy 非法（非 0~2，或 Dx+Dy < 1） |
| 30002 | Ksize 非法（非 1/3/5/7） |
| 30003 | Ddepth 非法（非 -1/CV_16S/CV_32F/CV_64F） |
| 30004 | 输入图像类型不支持 |
| 30005 | OpenCV 执行异常 |

---

#### cv_magnitude

梯度幅值计算，对应 `cv::magnitude`。

```
cv_magnitude(X, Y : Magnitude : :)
```

`Magnitude = sqrt(X² + Y²)`

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| X | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| Y | 图像 | 输入 | 与 X 同类型同尺寸 |
| Magnitude | 图像 | 输出 | 统一输出 `real`（CV_32F） |

> 输出统一为 `real`；`X`/`Y` 通常是 Sobel 的 x/y 梯度分量。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |

| 30002 | 两图类型不一致 |
| 30003 | 两图尺寸不一致 |
| 30004 | OpenCV 执行异常 |

---

#### CLAHE_image

限制对比度自适应直方图均衡化，对应 `cv::createCLAHE`。

```
CLAHE_image(Inimage : Outimage : K_width, K_height, ClipLimit)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Inimage | 图像 | 输入 | 单通道 `byte` / `uint2` |
| Outimage | 图像 | 输出 | 与输入同类型同尺寸 |
| K_width | 整数 | 输入 | 分块宽度 |
| K_height | 整数 | 输入 | 分块高度 |
| ClipLimit | 整数 | 输入 | 对比度裁剪阈值 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 参数非法（宽/高 ≤0 或 ClipLimit <0） |
| 30002 | 输入图像类型不支持 |
| 30004 | OpenCV 执行异常 |

---

### 算术类

#### cv_add_weighted

图像加权求和，对应 `cv::addWeighted`。

```
cv_add_weighted(ImageA, ImageB : ImageOut : Alpha, Beta, Gamma)
```

`ImageOut = saturate(Alpha * ImageA + Beta * ImageB + Gamma)`

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageB | 图像 | 输入 | 与 ImageA 同类型同尺寸 |
| ImageOut | 图像 | 输出 | 与输入同类型同尺寸 |
| Alpha | 实数 | 输入 | 权重，默认 1.0 |
| Beta | 实数 | 输入 | 权重，默认 1.0 |
| Gamma | 实数 | 输入 | 偏置，默认 0.0 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |
| 30002 | 两图类型不一致 |
| 30003 | 两图尺寸不一致 |
| 30004 | OpenCV 执行异常 |

---

#### 四则运算族：cv_add / cv_subtract / cv_multiply / cv_divide（含 `_masked` 变体）

对应 OpenCV `cv::add` / `cv::subtract` / `cv::multiply` / `cv::divide`，把原生签名里的 `mask`、`scale`、`dtype` 全部开放。

```
cv_add              (ImageA, ImageB : ImageOut : Dtype)
cv_subtract         (ImageA, ImageB : ImageOut : Dtype)
cv_multiply         (ImageA, ImageB : ImageOut : Scale, Dtype)
cv_divide           (ImageA, ImageB : ImageOut : Scale, Dtype)
cv_add_masked       (ImageA, ImageB, Mask : ImageOut : Dtype)
cv_subtract_masked  (ImageA, ImageB, Mask : ImageOut : Dtype)
cv_multiply_masked  (ImageA, ImageB, Mask : ImageOut : Scale, Dtype)
cv_divide_masked    (ImageA, ImageB, Mask : ImageOut : Scale, Dtype)
```

| 参数 | 类型 | 说明 |
|---|---|---|
| ImageA / ImageB | 图像 | 单通道 `byte` / `uint2` / `int4` / `real`，两图类型尺寸须一致 |
| Mask | 图像 | 仅 `_masked` 版本：`byte` 单通道、尺寸须与输入一致；**非零处参与运算，掩膜外像素保持 ImageA 原值** |
| ImageOut | 图像 | 类型由 `Dtype` 决定，尺寸同输入 |
| Scale | 实数/整数 | 仅 `multiply` / `divide`：运算前置系数，默认 1.0。`saturate(Scale·A·B)` / `saturate(Scale·A/B)`，整除向零取整、除零得 0 |
| Dtype | 整数/字符串 | `-1` 或 `'same'` 保持输入类型；`'u8'` / `'u16'` / `'s32'` / `'f32'` 输出 `byte` / `uint2` / `int4` / `real`；也接受 CV 深度整数 |

- `Dtype` 只支持有 HALCON 对应图像类型的深度：`'s8'` / `'s16'` / `'f64'` 报 30006。
- 整数输出**饱和截断**，实数输出**不截断**：`uint2` 1000−2000 → `0`，`'f32'` 下 → `-1000.0`。
- `multiply` / `divide` 的 OpenCV 原型没有 `mask` 参数，`_masked` 版本统一按"先算全图、再按掩膜拷回"实现，掩膜语义与 `cv::add` 一致。
- 例程：`examples/opencv/cv_arith.hdev`（8 个算子 + dtype/scale/掩膜 + 负例）、`examples/opencv/cv_subtract.hdev`。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |
| 30002 | 两图类型不一致 |
| 30003 | 两图尺寸不一致 |
| 30004 | OpenCV 执行异常 |
| 30005 | `Mask` 非 byte 单通道，或尺寸与输入不一致 |
| 30006 | `Dtype` 非法，或无 HALCON 对应图像类型 |

---

#### cv_mat_mul

两个 16 位单通道图像做矩阵乘法，对应 `cv::gemm`。

```
cv_mat_mul(ImageA, ImageB : Outimage : Alpha, Beta, Flags)
```

`C = alpha * A * B (+ beta)`，其中 `A: m×k`，`B: k×n`，结果 `C: m×n`。

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | 16 位单通道（m×k） |
| ImageB | 图像 | 输入 | 16 位单通道（k×n） |
| Outimage | 图像 | 输出 | 16 位单通道（m×n） |
| Alpha | 实数 | 输入 | 乘积系数，默认 1.0 |
| Beta | 实数 | 输入 | 叠加项系数，默认 0.0 |
| Flags | 字符串/整数 | 输入 | 转置标志（'none'/'transpose_a'/'transpose_b'/'transpose_c' 或 0/1/2/4） |

> 要求 A 的列数 == B 的行数；内部转 double 计算后饱和截断回 16 位。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 16 位单通道 |
| 30002 | 维度不匹配（A 列数 ≠ B 行数） |

---

#### ROI 算术

六个 ROI 算术算子均要求输入为 16 位单通道（`uint2`），结果**原地写入大图**（bigimage 的 ROI 区域被修改）。参数 `Sy`、`Sx` 为 ROI 起始行列，`Ew`、`Eh` 为 ROI 宽高。

| 算子 | 原型 | 运算 |
|---|---|---|
| add_roi | `add_roi(SmallImage, BigImage : : Sy, Sx, Ew, Eh)` | BigImage∩ROI += SmallImage |
| mul_roi | `mul_roi(SmallImage, BigImage : : Sy, Sx, Ew, Eh)` | BigImage∩ROI *= SmallImage |
| sub_B_roi | `sub_B_roi(SmallImage, BigImage : : Sy, Sx, Ew, Eh)` | BigImage∩ROI = SmallImage − BigImage∩ROI |
| div_B_roi | `div_B_roi(SmallImage, BigImage : : Sy, Sx, Ew, Eh)` | BigImage∩ROI = SmallImage / BigImage∩ROI |
| div_A_roi | `div_A_roi(SmallImage, BigImage : : Sy, Sx, Ew, Eh)` | BigImage∩ROI = BigImage∩ROI / SmallImage |
| sub_A_roi | `sub_A_roi(SmallImage, BigImage : : Sy, Sx, Ew, Eh)` | BigImage∩ROI = BigImage∩ROI − SmallImage |

错误码：`30000 + 内部错误码`，内部错误码含义：

| 内部错误码 | 含义 |
|---|---|
| 1 | SmallImage 非 16 位 |
| 2 | BigImage 非 16 位 |
| 3 | 坐标为负 |
| 4 | ROI 超出大图宽度 |
| 5 | ROI 超出大图高度 |
| 6 | SmallImage 宽度 ≠ ROI 宽 |
| 7 | SmallImage 高度 ≠ ROI 高 |

---

### 形状 / 几何变换类

#### cv_reshape

矩阵重塑，对应 `cv::Mat::reshape`（单通道）。

```
cv_reshape(Image : ImageOut : Rows)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageOut | 图像 | 输出 | 与输入同类型，新尺寸 |
| Rows | 整数 | 输入 | 输出图像新高度；0 表示保持原高度 |

`Cols = 总像素数 / Rows`（须能整除）。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |
| 30002 | Rows 为负数 |
| 30003 | 总像素数不能被 Rows 整除 |

---

#### remap

图像重映射，对应 `cv::remap`（字典接口）。

```
remap(:: hv_DictHandle)
```

字典 `hv_DictHandle` 键：

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| 输入图 | 图像 | 输入 | 16 位单通道 |
| 输出图 | 图像 | 输入/输出 | 16 位单通道（原地写入） |
| MapX | 图像 | 输入 | 32 位浮点 X 映射图 |
| MapY | 图像 | 输入 | 32 位浮点 Y 映射图 |

> 使用双线性插值（`INTER_LINEAR`）、常数边界填充 0（`BORDER_CONSTANT`）。

---

### 测量类

#### cv_measure_pos

复刻 HALCON `measure_pos`（一维边缘测量；算法本体在 cvr 库 `cvr/cvr_measure.hpp`，7 步管线：剖面提取→高斯一阶导核→卷积→局部极大值+阈值→抛物线亚像素→排序→去重/方向/映射）。

```
cv_measure_pos(Image : : Column, Row, Phi, Length1, Length2, Sigma, Threshold, Transition : RowEdge, ColumnEdge, Amplitude)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real`（内部转 byte） |
| Column | 实数 | 输入 | 测量矩形中心列坐标 x |
| Row | 实数 | 输入 | 测量矩形中心行坐标 y |
| Phi | 实数 | 输入 | 主轴角度（弧度，逆时针为正） |
| Length1 | 实数 | 输入 | 沿主轴半长 |
| Length2 | 实数 | 输入 | 垂直主轴半宽 |
| Sigma | 实数 | 输入 | 高斯平滑标准差 |
| Threshold | 实数 | 输入 | 振幅阈值 |
| Transition | 整数 | 输入 | 0=全部，1=暗→亮，-1=亮→暗 |
| RowEdge | 实数元组 | 输出 | 边缘行坐标 |
| ColumnEdge | 实数元组 | 输出 | 边缘列坐标 |
| Amplitude | 实数元组 | 输出 | 边缘振幅 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |

> **注意**：本实现对暗→亮（上升沿）边缘输出**负振幅**，亮→暗输出正振幅——即 `Transition=1` 实际选中亮→暗边缘，`-1` 选中暗→亮（保持与原实现兼容，暂未更改）。

---

### 多帧统计类

#### cv_multi_frame_median

对 N 张同尺寸 `real` 图像逐像素取中位数，输出一张 `real` 中值图（OpenCV 实现）。

```
cv_multi_frame_median(Images : ImageMedian : :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Images | 图像对象数组 | 输入 | N 张 `real` 单通道图，尺寸一致，N ≥ 2 |
| ImageMedian | 图像 | 输出 | `real` 单通道中值图 |

> N 为偶数时取上中位数（`vals[N/2]`）。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 图像数量 < 2 |
| 30002 | 输入图像非 `real` |
| 30003 | 图像尺寸不一致 |

---

### 特征检测与匹配类

以下四个算子均使用字典接口（`DictHandle`）。

#### cv_orb_detect

ORB 特征检测与描述子计算，对应 `cv::ORB`。

```
cv_orb_detect(:: DictHandle)
```

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| InputImage | 图像 | 输入 | 8 位单通道 |
| NFeatures | 元组 | 输入 | 特征点数量上限，默认 3000 |
| ScaleFactor | 元组 | 输入 | 金字塔缩放因子，默认 1.2 |
| NLevels | 元组 | 输入 | 金字塔层数，默认 8 |
| EdgeThreshold | 元组 | 输入 | 边界阈值，默认 31 |
| FirstLevel | 元组 | 输入 | 首层金字塔层，默认 0 |
| WTA_K | 元组 | 输入 | WTA_K 点数，默认 2 |
| ScoreType | 元组 | 输入 | 评分类型（0=HARRIS / 1=FAST，默认 0） |
| PatchSize | 元组 | 输入 | 描述子邻域大小，默认 31 |
| FastThreshold | 元组 | 输入 | FAST 角点阈值，默认 20 |
| KeypointsRow | 元组 | 输出 | 特征点行坐标 |
| KeypointsCol | 元组 | 输出 | 特征点列坐标 |
| NumKeypoints | 元组 | 输出 | 特征点数量 |
| Descriptors | 图像 | 输出 | 描述子（byte，32×N） |

---

#### cv_akaze_detect

AKAZE 特征检测与描述子计算，对应 `cv::AKAZE`。

```
cv_akaze_detect(:: DictHandle)
```

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| InputImage | 图像 | 输入 | 8 位单通道 |
| DescriptorType | 元组 | 输入 | 描述子类型（0=KAZE/1=KAZE_UPRIGHT/2=MLDB_UPRIGHT/4=MLDB，默认 4） |
| Threshold | 元组 | 输入 | 检测阈值，默认 0.001 |
| NOctaves | 元组 | 输入 | 八度组数，默认 4 |
| NOctaveLayers | 元组 | 输入 | 每组层数，默认 4 |
| Diffusivity | 元组 | 输入 | 扩散方式（0=PM_G1/1=PM_G2/2=WEICKERT/3=CHARBONNIER，默认 1） |
| KeypointsRow | 元组 | 输出 | 特征点行坐标 |
| KeypointsCol | 元组 | 输出 | 特征点列坐标 |
| NumKeypoints | 元组 | 输出 | 特征点数量 |
| Descriptors | 图像 | 输出 | 描述子（byte） |
| DescWidth | 元组 | 输出 | 描述子宽度 |

---

#### cv_sift_detect

SIFT 特征检测与描述子计算，对应 `cv::SIFT`。

```
cv_sift_detect(:: DictHandle)
```

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| InputImage | 图像 | 输入 | 8 位单通道 |
| NFeatures | 元组 | 输入 | 特征点数量上限，默认 0（不限制） |
| NOctaveLayers | 元组 | 输入 | 每八度层数，默认 3 |
| ContrastThreshold | 元组 | 输入 | 对比度阈值，默认 0.04 |
| EdgeThreshold | 元组 | 输入 | 边缘阈值，默认 10.0 |
| Sigma | 元组 | 输入 | 高斯 sigma，默认 1.6 |
| KeypointsRow | 元组 | 输出 | 特征点行坐标 |
| KeypointsCol | 元组 | 输出 | 特征点列坐标 |
| NumKeypoints | 元组 | 输出 | 特征点数量 |
| Descriptors | 图像 | 输出 | 描述子（byte，128×N） |
| DescWidth | 元组 | 输出 | 描述子宽度 |

---

#### cv_bf_knn_match

BF 暴力匹配 + Lowe's Ratio Test，对应 `cv::BFMatcher`（汉明距离）。

```
cv_bf_knn_match(:: DictHandle)
```

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| DescriptorsRef | 图像 | 输入 | 参考描述子（byte） |
| DescriptorsTarget | 图像 | 输入 | 目标描述子（byte） |
| DescWidth | 元组 | 输入 | 描述子宽度，默认 32 |
| RatioThresh | 元组 | 输入 | 比值阈值，默认 0.75 |
| NormType | 元组 | 输入 | 距离范数（1=L1/2=L2/4=NORM_HAMMING，默认 4） |
| CrossCheck | 元组 | 输入 | 交叉验证（0=否走 Ratio Test / 1=是走 match，默认 0） |
| K | 元组 | 输入 | knnMatch 的 k（Ratio Test 需 >=2，默认 2） |
| MatchIdxRef | 元组 | 输出 | 匹配上的参考索引 |
| MatchIdxTarget | 元组 | 输出 | 匹配上的目标索引 |
| NumGoodMatches | 元组 | 输出 | 良好匹配数量 |

---

#### cv_estimate_affine_partial2d

部分仿射变换估计（RANSAC），对应 `cv::estimateAffinePartial2D`。

```
cv_estimate_affine_partial2d(:: DictHandle)
```

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| SrcRow / SrcCol | 元组 | 输入 | 源点行/列坐标 |
| DstRow / DstCol | 元组 | 输入 | 目标点行/列坐标 |
| RansacThreshold | 元组 | 输入 | RANSAC 阈值，默认 3.0 |
| Method | 元组 | 输入 | 估计方法（8=RANSAC / 4=LMEDS，默认 8） |
| MaxIters | 元组 | 输入 | RANSAC 最大迭代，默认 2000 |
| Confidence | 元组 | 输入 | 置信度，默认 0.99 |
| RefineIters | 元组 | 输入 | 精炼迭代次数，默认 10 |
| HomMat2D | 元组 | 输出 | 仿射矩阵（2×3 展开） |
| Success | 元组 | 输出 | 是否成功 |
| InlierCount | 元组 | 输出 | 内点数量 |
| TranslateRow / TranslateCol | 元组 | 输出 | 平移量 |
| Angle | 元组 | 输出 | 旋转角度 |
| Scale | 元组 | 输出 | 缩放 |

---

#### cv_estimate_rigid_2d

刚体变换估计（平移 + 旋转，无缩放，RANSAC），对应 `cv::estimateAffinePartial2D` 的刚体特例。

```
cv_estimate_rigid_2d(:: DictHandle)
```

| 键 | 类型 | 方向 | 说明 |
|---|---|---|---|
| SrcRow / SrcCol | 元组 | 输入 | 源点行/列坐标 |
| DstRow / DstCol | 元组 | 输入 | 目标点行/列坐标 |
| RansacThreshold | 元组 | 输入 | RANSAC 阈值，默认 3.0 |
| MaxIter | 元组 | 输入 | RANSAC 最大迭代，默认 500 |
| Seed | 元组 | 输入 | 随机种子，默认 12345 |
| HomMat2D | 元组 | 输出 | 刚体矩阵（2×3 展开） |
| Success | 元组 | 输出 | 是否成功 |
| InlierCount | 元组 | 输出 | 内点数量 |
| TranslateRow / TranslateCol | 元组 | 输出 | 平移量 |
| Angle | 元组 | 输出 | 旋转角度 |
| Scale | 元组 | 输出 | 恒为 1.0 |

---

### 图像 I/O 类

#### cv_write_image

将图像保存为 PNG 文件。

```
cv_write_image(Inimage : : Filename, Compression)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Inimage | 图像 | 输入 | 8/16 位，单/三通道 |
| Filename | 字符串 | 输入 | 输出文件路径 |
| Compression | 整数 | 输入 | PNG 压缩级别，默认 3 |

---

#### PNGIn

将图像编码为 PNG 并打包进一张 HALCON「乱码图」（8 字节大小 + 1 字节通道数 + PNG 数据）。

```
PNGIn(Inimage : Outimage : Acceleration)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Inimage | 图像 | 输入 | 8/16 位，单/三通道 |
| Outimage | 图像 | 输出 | byte「乱码图」 |
| Acceleration | 整数 | 输入 | PNG 压缩级别 |

---

#### PNGOut

从「乱码图」中解码 PNG，还原 1 或 3 通道图像。

```
PNGOut(Inimage : Outimage : :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Inimage | 图像 | 输入 | byte「乱码图」 |
| Outimage | 图像 | 输出 | 还原后的图像（8/16 位，1/3 通道） |

---

### 元数据类

#### write_image_exif

向图像文件写入 EXIF 元数据（GPS、光圈、快门、ISO、焦距、时间、相机信息），基于 exiv2。

```
write_image_exif(:: ImagePath, Latitude, Longitude, Altitude, Aperture, ShutterSpeed, IsoNumber, FocalLength, DateTime, CameraMake, CameraModel)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImagePath | 字符串 | 输入 | 图像文件路径 |
| Latitude | 实数 | 输入 | 纬度 |
| Longitude | 实数 | 输入 | 经度 |
| Altitude | 实数 | 输入 | 海拔（米） |
| Aperture | 实数 | 输入 | 光圈值 |
| ShutterSpeed | 实数 | 输入 | 快门速度（秒） |
| IsoNumber | 整数 | 输入 | ISO |
| FocalLength | 实数 | 输入 | 焦距（mm） |
| DateTime | 字符串 | 输入 | 拍摄时间 |
| CameraMake | 字符串 | 输入 | 相机厂商 |
| CameraModel | 字符串 | 输入 | 相机型号 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 写入失败 |

---

### 线性方程组与矩阵类

#### cv_solve

解线性方程组 A\*x = b，对应 `cv::solve`。矩阵用 real 单通道图像承载（height = 行数，width = 列数）。

```
cv_solve(ImageA, ImageB : ImageX : MethodId)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | real 单通道矩阵 A（m×n） |
| ImageB | 图像 | 输入 | real 单通道矩阵 b（m×1 或 m×k） |
| ImageX | 图像 | 输出 | 解 x（n×1 或 n×k，real） |
| MethodId | 整数 | 输入 | 求解方法，默认 0 |

`MethodId` 取值：`DECOMP_LU=0` / `DECOMP_SVD=1` / `DECOMP_EIG=2` / `DECOMP_CHOLESKY=3` / `DECOMP_QR=4` / `DECOMP_NORMAL=16`

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 real 单通道矩阵 |
| 30002 | b 的行数 ≠ A 的行数 |
| 30003 | 求解失败（奇异 / 维度不匹配） |
| 30004 | OpenCV 执行异常 |

---

### 几何估计类

#### cv_estimate_affine_2d

完整仿射变换估计（6 参数），对应 `cv::estimateAffine2D`。点坐标用 tuple 输入输出。

```
cv_estimate_affine_2d(:: MethodId, RansacThreshold, SrcRow, SrcCol, DstRow, DstCol, MaxIters, Confidence, RefineIters : HomMat2D, Success, InlierCount)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| MethodId | 整数 | 输入 | `RANSAC=8`（默认）/ `LMEDS=4` |
| RansacThreshold | 实数 | 输入 | 重投影阈值，默认 3.0 |
| SrcRow / SrcCol | 实数元组 | 输入 | 源点行 / 列坐标 |
| DstRow / DstCol | 实数元组 | 输入 | 目标点行 / 列坐标 |
| MaxIters | 整数 | 输入 | RANSAC 最大迭代次数，默认 2000 |
| Confidence | 实数 | 输入 | 置信度，默认 0.99 |
| RefineIters | 整数 | 输入 | 最小二乘精炼迭代次数，默认 10 |
| HomMat2D | 实数元组 | 输出 | 6 元素仿射矩阵 `[R00,R10,T0,R01,R11,T1]` |
| Success | 整数 | 输出 | 1 = 成功，0 = 失败 |
| InlierCount | 整数 | 输出 | 内点数量 |

> 输出顺序与 HALCON `hom_mat2d` 一致，可直接用于 `affine_trans_point_2d` 等算子。

---

### 阈值与直方图类

#### cv_threshold_triangle

三角法自动阈值，对应 `cv::threshold` + `THRESH_TRIANGLE`。

```
cv_threshold_triangle(Image : ImageOut : : ThreshValue)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | **仅 8 位单通道**（OpenCV 限制） |
| ImageOut | 图像 | 输出 | 二值图（byte，0 / 255） |
| ThreshValue | 实数 | 输出 | 自动计算出的阈值 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 8 位单通道 |
| 30002 | OpenCV 执行异常 |

---

#### cv_calc_hist

灰度直方图，对应 `cv::calcHist`。

```
cv_calc_hist(Image : Histogram : HistSize, RangeMin, RangeMax)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| Histogram | 图像 | 输出 | real，1×HistSize（各桶计数） |
| HistSize | 整数 | 输入 | 直方图桶数，默认 256 |
| RangeMin / RangeMax | 实数 | 输入 | 像素值范围，默认 0.0 / 256.0 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 桶数非法（≤0） |
| 30002 | 输入图像类型不支持 |
| 30003 | OpenCV 执行异常 |

---

### 匹配与聚类类

#### cv_match_template

模板匹配，对应 `cv::matchTemplate` + `TM_CCOEFF_NORMED`。

```
cv_match_template(Image, TemplateImage : Result : MethodId)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `real`（OpenCV 限制，不支持 16 位） |
| TemplateImage | 图像 | 输入 | 与 Image 同类型，尺寸 ≤ Image |
| Result | 图像 | 输出 | real 得分图，尺寸 (W−w+1)×(H−h+1) |
| MethodId | 字符串/整数 | 输入 | 匹配方法（'sqdiff'/'sqdiff_normed'/'ccorr'/'ccorr_normed'/'ccoeff'/'ccoeff_normed' 或 0-5，默认 5） |

> 得分范围为 [−1, 1]，最佳匹配位置得分为 1。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |
| 30002 | 模板类型与图像不一致 |
| 30003 | 模板尺寸大于图像 |
| 30004 | OpenCV 执行异常 |

---

#### cv_kmeans

K 均值聚类，对应 `cv::kmeans`。

```
cv_kmeans(Samples : Labels, Centers : K, Attempts, TermEps, TermMaxIter, Flags)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Samples | 图像 | 输入 | real N×D（N 个样本，每行 D 维特征） |
| Labels | 图像 | 输出 | real N×1（整数标签，以浮点承载） |
| Centers | 图像 | 输出 | real K×D 聚类中心 |
| K | 整数 | 输入 | 聚类数，默认 2 |
| Attempts | 整数 | 输入 | 重复尝试次数，默认 10 |
| TermEps | 实数 | 输入 | 终止精度，默认 1.0e-4 |
| TermMaxIter | 整数 | 输入 | 最大迭代次数，默认 100 |
| Flags | 字符串/整数 | 输入 | 中心初始化（'random'/'pp'/'use_initial_labels' 或 0/1/2，默认 1） |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 样本矩阵非 real |
| 30002 | K 非法（≤0 或 ＞ 样本数） |
| 30003 | OpenCV 执行异常 |

---

### 灰度形态学

灰度（及二值）图像形态学运算，对应 `cv::morphologyEx`，支持 `byte` / `uint2` / `real` 单通道。预设算子与 region 侧命名一致（矩形 Width/Height，圆 Radius→2R+1 椭圆核）：

| 算子 | 结构元 |
|---|---|
| `cv_gray_erosion_rect` / `cv_gray_dilation_rect` | 矩形 |
| `cv_gray_opening_rect` / `cv_gray_closing_rect` | 矩形 |
| `cv_gray_erosion_circle` / `cv_gray_dilation_circle` | 圆 |
| `cv_gray_opening_circle` / `cv_gray_closing_circle` | 圆 |

通用算子（字符串枚举 Op/Shape，覆盖 gradient/tophat/blackhat 与 cross 结构元）：

```
cv_morphology_ex(Image : ImageOut : Op, Shape, Kwidth, Kheight, Iterations :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Op | 字符串 | 输入 | erode/dilate/open/close/gradient/tophat/blackhat（也可整数 0-6） |
| Shape | 字符串 | 输入 | rect/cross/ellipse（也可整数 0-2） |
| Kwidth / Kheight | 整数 | 输入 | 核尺寸，≥1 |
| Iterations | 整数 | 输入 | 迭代次数 ≥1 |

错误码：30001 图像类型不支持；30002 OpenCV 异常；30003 参数非法。

---

### 算子支持类型速查表

| 算子 | byte | uint2 | real | 多通道 |
|---|---|---|---|---|
| cv_blur | ✅ | ✅ | ✅ | ❌ |
| cv_median_blur | ✅ | ✅(3/5) | ✅(3/5) | ❌ |
| cv_filter2d | ✅ | ✅ | ✅ | ❌ |
| cv_sep_filter2d | ✅ | ✅ | ✅ | ❌ |
| CLAHE_image | ✅ | ✅ | ❌ | ❌ |
| cv_add_weighted | ✅ | ✅ | ✅ | ❌ |
| cv_add / cv_subtract / cv_multiply / cv_divide（含 `_masked`） | ✅ | ✅ | ✅ | ❌（另支持 `int4`） |
| cv_mat_mul | ❌ | ✅ | ❌ | ❌ |
| ROI 算术 | ❌ | ✅ | ❌ | ❌ |
| cv_reshape | ✅ | ✅ | ✅ | ❌ |
| remap | ❌ | ✅ | ❌ | ❌ |
| cv_measure_pos | ✅ | ✅ | ✅ | ❌ |
| cv_orb_detect | ✅ | ❌ | ❌ | ❌ |
| cv_akaze_detect | ✅ | ❌ | ❌ | ❌ |
| cv_sift_detect | ✅ | ❌ | ❌ | ❌ |
| cv_write_image | ✅ | ✅ | ❌ | ✅(3) |
| PNGIn | ✅ | ✅ | ❌ | ✅(3) |
| PNGOut | ✅ | ✅ | ❌ | ✅(3) |
| cv_solve | ❌ | ❌ | ✅ | ❌ |
| cv_estimate_affine_2d | — | — | — | — |
| cv_estimate_rigid_2d | — | — | — | — |
| cv_threshold_triangle | ✅ | ❌ | ❌ | ❌ |
| cv_calc_hist | ✅ | ✅ | ✅ | ❌ |
| cv_match_template | ✅ | ❌ | ✅ | ❌ |
| cv_kmeans | ❌ | ❌ | ✅ | ❌ |
| cv_multi_frame_median | ❌ | ❌ | ✅ | ❌ |
| cv_sobel | ✅ | ✅ | ✅ | ❌ |
| cv_magnitude | ✅ | ✅ | ✅ | ❌ |

---

## Region 扩展算子（cv_region）

本扩展包的 region 运算算子（定义于 `def/Halcon_CVRegion.def`，实现于 `source/Halcon_CVRegion.cpp`），算法本体在独立开源库 **cv_region**（`cv_region/` 子目录，**静态链接**为 `cvr_core`，直接编入扩展包，免跨 DLL 拷贝，可脱离 HALCON 单独调用）。HALCON 语义兼容：RLE chord 编码 `{row, col_begin, col_end}`、形态学 SE 参考点 = 质心四舍五入。

- 集合运算：cv_union1 / cv_union2 / cv_intersection / cv_difference / cv_complement / cv_symm_difference
- 形态学（8 个，仅矩形/圆结构元）：cv_erosion_circle / cv_dilation_circle / cv_opening_circle / cv_closing_circle（半径）+ cv_erosion_rectangle1 / cv_dilation_rectangle1 / cv_opening_rectangle1 / cv_closing_rectangle1（宽高）
- 连通域：cv_connection / cv_connection_ex（后者可在标记同时预计算形状特征并缓存）；生成：cv_gen_circle / cv_gen_rectangle1；填充：cv_fill_up；形状变换：cv_shape_trans；筛选：cv_select_shape
- 特征：cv_area_center / cv_smallest_rectangle1 / cv_smallest_rectangle2 / cv_smallest_circle / cv_elliptic_axis / cv_contlength / cv_circularity / cv_compactness / cv_convexity / cv_rectangularity / cv_anisometry / cv_bulkiness / cv_structure_factor
- 互转：cv_region_to_bin / cv_bin_to_region（cv_bin_to_region 支持 byte/int1/int2/uint2/int4/int8/real 单通道，Threshold 为 real/integer）

#### cv_connection_ex

带**形状特征预计算缓存**的连通域标记。标记语义与 `cv_connection` 完全一致（8 连通、RLE chord 编码、并查集），额外可在标记时把指定形状特征算好并写入**跨算子内容指纹缓存**，供后续 `cv_region_features` / `cv_select_shape` / `cv_area_center` / `cv_smallest_rectangle1` / `cv_smallest_rectangle2` / `cv_smallest_circle` / `cv_elliptic_axis` / `cv_contlength` / `cv_circularity` / `cv_compactness` / `cv_convexity` / `cv_rectangularity` 直接命中，免重复扫描 region。

```
cv_connection_ex(Region : ConnectedRegions : CacheFeatures :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Region | region | 输入 | 输入 region（连通域标记） |
| ConnectedRegions | region | 输出 | 连通分量元组，与 `cv_connection` 逐元素一致 |
| CacheFeatures | 字符串元组 | 输入 | 需预计算的特征名，缺省 `none`（此时与 `cv_connection` 等价） |

`CacheFeatures` 取值（HALCON 层一律用**字符串**，名称与 `cv_region_features` 同口径）：

| 类别 | 取值 |
|---|---|
| 单特征（按一次遍历分组） | `area` `center` `row` `column` `col` `bbox` `rectangle1` `smallest_rectangle1` `width` `height` `ratio` `row1` `column1` `row2` `column2` `rect2` `rectangle2` `smallest_rectangle2` `phi_rect` `length1` `length2` `circle` `smallest_circle` `radius` `contlength` `convexity` `circularity` `compactness` `rectangularity` `moments` `elliptic_axis` `ra` `rb` `phi` `excentricity` `anisometry` `bulkiness` `structure_factor`（别名 `struct_factor`） |
| 组合名 | `none`（不缓存）· `basic` / `cheap`（=`area+row+column+bbox+moments+contlength`）· `hull`（=`convexity+rect2+circle`）· `all`（全部 12 组，含依赖项） |
| `\|` 拼接 | 元素内可拼接，如 `'area\|bbox'`、`'all\|contlength'`（重复位自动合并） |

- 名称大小写不敏感、允许前后空白；未知名报错 **10102**，元素为空串报错 **10103**。
- 特征按**依赖关系**自动补齐（如 `circularity` 会带上 `convexity` + `contlength` + `area`，`rectangularity` 会带上 `convexity` + `rectangle2`）。
- **缓存是进程内的**，按 region **内容指纹**（chord 数据 + 是否补集）分片存放（16 分片 × 512 条 FIFO 淘汰），命中即把已算好的特征"水合"到本次调用重建的 region 结构上；因为所有形状特征都与定义域 `w/h` 无关，指纹无需包含图像尺寸。
- **未预计算的特征照旧按需计算并回写缓存**——即首次 `cv_region_features` 会顺带填充缓存，之后的重复调用、以及 `cv_select_shape` 的多次比较都直接命中。region 内容一变（平移 / 形态学 / 集合运算等），指纹随之改变，自动按新内容计算，不存在陈旧命中。
- 例程：`examples/cv_region_cache.hdev`（37 个连通域 × 8 特征：预计算 vs 冷算逐值一致 ≤1e-12、四种掩码变体计数一致、`cv_select_shape` 双路径一致、`move_region` 后与 HALCON 原生 `area_center` 逐值对照防陈旧；实测重复取特征 **1.88 ms → 0.12 ms（15.7×）**，一次性预计算 2.85 ms）。

> **选型建议（实测，`examples/cv_region_cache.hdev`）**：预计算只对"同一批 region 被反复取特征"有利；**单趟流水线（`connection → select_shape → region_features` 各一次）反而更慢**——实测同一条链预计算 8 个特征 **0.44 ms → 3.05 ms（慢 6.9×）**，原因是 `select_shape` 的 `and` 分支会短路（`area` 不过关就不算 `circularity`）、且 `region_features` 只需为**选中的 28/37** 个区域算，而 `cv_connection_ex` 得为**全部 37 个**区域把整组特征算完。
>
> 结论：**默认用 `cv_connection`，让"按需计算 + 自动回写"的惰性缓存（无需任何参数）吃掉重复调用**；只有当你明确会对同一批成分多次调用特征算子时，才用 `cv_connection_ex` 指定掩码（建议只写真正要用的特征，例如 `'area|bbox|circularity'`，别图省事写 `'all'`）。

#### cv_bin_to_region

数值类型图像转 region（`gray >= Threshold` 的像素），对应 HALCON `threshold` 的逆过程。

```
cv_bin_to_region(BinImage : Region : Threshold :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| BinImage | 图像 | 输入 | 单通道数值类型图像：`byte` / `int1` / `int2` / `uint2` / `int4` / `int8` / `real` |
| Region | region | 输出 | 前景区域 |
| Threshold | 实数/整数 | 输入 | 前景阈值，`gray >= Threshold` 为前景；`real` 图像可用小数阈值（如 `0.75`） |

- 支持 16 位（`int2` / `uint2`）与浮点（`real`）图像；比较在 `double` 下进行，与像素位深（`num_bits`）无关，语义与 HALCON 原生 `threshold`（下界包含）一致。
- `direction` / `cyclic` / `complex` / `vector_field` 等非数值灰度类型报错 10103（参数非法）。
- 例程：`examples/cv_region_all.hdev`（6 种类型逐个与 HALCON 原生 `threshold` 对照 + 非数值类型守卫）。

#### cv_region_features

按名称一次算出多个形状特征，对应 HALCON `region_features`；实现直接封装核心库 `cvr::cvr_region_features`（内部即 `cvr_get_feature` 查表），与 `cv_select_shape` 共用同一套特征名口径。

```
cv_region_features(Regions : : Features : Values)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Regions | region | 输入 | 输入 region 元组，逐元素计算 |
| Features | 字符串元组 | 输入 | 特征名元组，取值与 `cv_select_shape` 完全一致 |
| Values | 实数元组 | 输出 | 特征值，平铺为「区域数 × 特征数」，**区域优先**（同一区域的各特征值相邻） |

- `Features` 支持 **54 个名称**，取值表与 HALCON `region_features` / `select_shape` 对齐：
  - 基础：`area` / `row` / `column` / `width` / `height` / `ratio` / `row1` / `column1` / `row2` / `column2`
  - 形状：`circularity` / `compactness` / `convexity` / `rectangularity` / `contlength` / `phi` / `ra` / `rb` / `anisometry` / `bulkiness` / `struct_factor`（别名 `structure_factor`，HALCON 正式名为 `struct_factor`）
  - 外接/直径：`outer_radius` / `max_diameter` / `rect2_phi` / `rect2_len1` / `rect2_len2`
  - 轮廓距离：`dist_mean` / `dist_deviation` / `roundness` / `num_sides`
  - 连通性/孔洞：`connect_num` / `holes_num` / `area_holes` / `euler_number`
  - 矩（18 个）：`moments_m11` / `m20` / `m02` / `ia` / `ib` / `m11_invar` / `m20_invar` / `m02_invar` / `phi1` / `phi2` / `m21` / `m12` / `m03` / `m30` / `m21_invar` / `m12_invar` / `m03_invar` / `m30_invar`
  - 核心库另有 `row_rect` / `column_rect` / `phi_rect` / `length1` / `length2` / `row_circle` / `column_circle` / `radius` 等内部别名，未列入 DEF 的 `value_list`
- 未知名 / 计算失败报错 10102，`Features` 为空报错 10103。
- **缓存命中**：同一内容若已被 `cv_connection_ex` 预计算过（或在其它算子调用中被回写），本算子直接查表回填、不再扫描 region（`examples/cv_region_cache.hdev` 实测 15.7× 加速）；本算子自身算完的特征也会回写缓存，缓存机制与指纹口径见 `cv_connection_ex`。
- 例程：`examples/cv_region_all.hdev`（408 区域 × 20 特征 = 8160 值，校验长度与行优先布局；几何类 + 25 个新特征与 HALCON 原生 `region_features` 对照——新特征实测 max rel ≤ 1.5e-12）、`examples/cv_region_cache.hdev`。

**本次补齐的 32 个 HALCON 对齐特征**（`Features` 取值表已同步到两个算子的 DEF `value_list`）：

| 分组 | 特征 | 与 HALCON 实测 |
|---|---|---|
| 基础/几何 | `ratio`、`outer_radius`、`max_diameter`、`rect2_phi`、`rect2_len1`、`rect2_len2` | `ratio` / `outer_radius` / `max_diameter` / `rect2_len1` / `rect2_len2` ≤1e-12；`rect2_phi` 符号相反 |
| 连通性/孔洞 | `connect_num`、`holes_num`、`area_holes`、`euler_number` | 全 408 区域**完全一致**（前景 8 邻域、背景 4 邻域的连通性对偶） |
| 矩（18 个） | `moments_m11` / `m20` / `m02` / `ia` / `ib` / `m11_invar` / `m20_invar` / `m02_invar` / `phi1` / `phi2` / `m21` / `m12` / `m03` / `m30` / `m21_invar` / `m12_invar` / `m03_invar` / `m30_invar` | ≤1.5e-12（多数为 0）；口径 = HALCON `moments_region_2nd(_invar/_rel_invar/_3rd/_3rd_invar)` 文档公式 |
| 朝向 | `orientation` | 符号相反（基于 `elliptic_axis` + 最远轮廓点，同 HALCON 语义） |
| 轮廓距离 | `dist_mean`、`dist_deviation`、`roundness`、`num_sides` | **口径不同**：用内边界像素（等价 HALCON `boundary 'inner'`）+ HALCON `roundness` 文档公式；HALCON 内部用其轮廓链口径，实测大区域（面积 ≥ 200）偏差 ≤8.5%，小区域更大 |

> **phi 系符号**：`phi` / `rect2_phi` / `orientation` 的绝对值与 HALCON 一致但**符号相反**（核心库既有 phi 约定：我们的 phi = −HALCON phi），例程用构造旋转矩形做了确定性验证。

> **已知偏差（核心库既有，非本算子引入）**：`contlength` / `circularity` / `compactness` / `convexity` / `rectangularity` / `phi` / `struct_factor` 与 HALCON 原生数值不一致（如 51×81 实心矩形：`contlength` 260 → 183，因核心库漏计最后一行下边界）。`cv_contlength` / `cv_circularity` / `cv_compactness` / `cv_structure_factor` 等专用算子同样受影响，`cv_region_features` 只是原样继承。`area` / `row` / `column` / `width` / `height` / `row1`–`column2` / `ra` / `rb` / `anisometry` / `bulkiness` 已与 HALCON 1e-9 一致。
> `rect2_len1` / `rect2_len2` 在**极小区域**（面积 ≤ 10 px 的退化形状）与 HALCON 有算法固有差异（HALCON 取到更小的等价解）。
> 顺带修掉核心库两处既有缺陷：① `smallest_circle` 的启发式迭代会发散（非凸区域半径偏大，实测 area=1490 的 blob 67.66 → 修后与 HALCON 一致 47.07；已换成精确的增量式最小外接圆算法），② 孔洞统计漏了**连通性对偶**（背景必须用 4 邻域，否则对角夹缝被误判成孔洞）。两者都影响 `cv_smallest_circle` / `cv_region_features` 等既有算子。

> **待办（下一轮）**：`inner_radius` / `inner_width` / `inner_height`（需最大内接圆 / 内接轴对齐矩形算法）、`moments_i1`–`i4` / `moments_psi1`–`psi4`（三阶矩旋转不变量，HALCON 未公开公式）、`num_sides` 与 HALCON 的精确对齐。

#### cv_shape_trans

region 形状变换，对应 HALCON `shape_trans`。

```
cv_shape_trans(Region : RegionTrans : Type :)
```

| Type | 含义 | 与 HALCON 的一致性 |
|---|---|---|
| `rectangle1` | 最小轴对齐外接矩形 | ✅ 逐像素一致（对称差面积 = 0） |
| `rectangle2` | 最小外接旋转矩形 | ✅ 轴对齐档逐像素一致；倾斜档面积差 ≤5%、对称差 ≈3%（HALCON 边界栅格化略宽松） |
| `ellipse` | 等效椭圆 | ⚠️ 面积约为 HALCON 的 1/4.5 |
| `outer_circle` | 最小外接圆 | ⚠️ 面积明显偏小 |
| `convex` | 凸包 | ⚠️ 面积略大于 HALCON（对称差 ≈3%） |

- 未实现 `inner_circle` / `inner_rectangle1`。
- 生成 region 的栅格化口径：像素 `(r,c)` 视为方格 `[r,r+1)×[c,c+1)`，行取 `floor(min_row)..floor(max_row)`，每行在 `y = row` 处求多边形列跨度后取 `floor`。实测该口径在轴对齐图形上可逐像素复现 HALCON。
- 例程：`examples/cv_region_all.hdev`（`rectangle2` 倾斜档 + 90° 轴对齐档与 HALCON 原生对照）。

> **已知偏差（核心库既有）**：`ellipse` / `outer_circle` 共用 `cvr_shape::add_ellipse_runs`——它只**采样边界点**再逐行取 min/max，采样稀疏的行会退化成 1 像素宽甚至整行丢失，故圆/椭圆都填不满；`ellipse` 还在 `elliptic_axis` 的 `Ra/Rb` 上又乘了 0.5，面积再小 4 倍。`convex` 用 `floor/ceil` 填充，比 HALCON 略大。
> `rectangle2` 已于本次修复：原实现用 `u = 0.5*w*cos(t); v = 0.5*h*sin(t)` 沿半宽/半高采样一圈——那是**内切椭圆**，所以输出成椭圆/圆。同时修掉 `cvr_feature_smallest_rectangle2` 的一处主轴错误（`length1` 取长边却恒用 `u` 轴角作 `phi`，当 `h > w` 时 `phi` 差 90°，轴对齐矩形两种候选面积打平时会命中，表现为 `rectangle2` 生成旋转 90° 的矩形）。该修正同时影响 `cv_smallest_rectangle2` 在同等情形下的 `Phi` 输出（原先错误，现与 HALCON 的 `±π/2` 语义一致）。

#### cv_gen_rectangle2

生成旋转矩形 region，对应 HALCON `gen_rectangle2`。核心库 `cvr::cvr_gen_rectangle2`。

```
cv_gen_rectangle2(Rectangle2 : : Row, Column, Phi, Length1, Length2 :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Rectangle2 | region | 输出 | 生成的旋转矩形 |
| Row / Column | 实数/整数 | 输入 | 矩形中心坐标 |
| Phi | 实数/整数 | 输入 | 主轴与列轴夹角（弧度，**HALCON 约定**） |
| Length1 / Length2 | 实数/整数 | 输入 | 两个方向的半边长（`Length1` 沿主轴，均 ≥ 0） |

- 栅格化口径 = **像素方格相交**（像素 `(r,c)` 覆盖 `[r-0.5,r+0.5]×[c-0.5,c+0.5]`，与矩形有交集即取）。
- 与 HALCON 原生 `gen_rectangle2` 实测对比：轴对齐、`Phi=π/2`、负中心裁剪三档**逐像素一致（对称差 = 0）**；倾斜档面积差 ≈1.8%、对称差 ≈1.8%；极小/退化图形（`Length=0`、半边长 3 之类）差 1 像素量级（如 HALCON 45 → 本算子 53）。
- **Phi 口径**：本算子用 cv/OpenCV 口径（`cvr` 原始 `phi`，不取反）；因此与 HALCON `gen_rectangle2` 的 `phi` **反号**（同参数生成绕列轴镜像的矩形；`phi=0` 与 `±π/2` 因矩形对称不受影响）。`cv_smallest_rectangle2` / `cv_elliptic_axis` 返回的 `phi` 也是这个口径。
- 参数为标量：不像原生那样支持元组批量（与包内 `cv_gen_circle` / `cv_gen_rectangle1` 一致）。
- 与 `cv_shape_trans 'rectangle2'` 口径不同（后者按 HALCON `shape_trans` 的 polygon rasterizer），同一参数下两者面积可差约 5%。
- 例程：`examples/cv_region_all.hdev`（轴对齐 / 90° / 倾斜三档与 HALCON 原生对照）。

性能：与 HALCON 原生算子同量级（erosion 约 1.8×、connection 约 3×，见 `examples/cv_region_bench.hdev`）。

---

## RANSAC 扩展算子（cv_flow 几何拟合）

RANSAC 通用几何拟合（定义于 `def/Halcon_Ransac.def`），算法本体在 **cv_flow 静态库**（`cvflow::`，`cv_flow/src/ransac.cpp` 的 `ransac_create` / `ransac_fit`：muparser 表达式模型 + Eigen LM 求解），supply 直调 C++ API。采用**两步式**：建模一次、反复拟合。

```
cv_geom_create(:: ModelExpression, XName, YName, ParamNames, ModelType, ResidualType : ModelHandle)
cv_geom_fit(:: ModelHandle, XData, YData, InitialValues, Threshold, MaxIter, OutlierRatio, Confidence, Seed : ParamValues, InlierMask, ResidualSum, Iterations, Status, StatusMessage, InlierRatio)
```

### cv_geom_create

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ModelExpression | 字符串 | 输入 | 模型表达式：显式 `y=f(x)` 或隐式 `F(x,y)=0`（圆 / 椭圆 / 圆锥曲线） |
| XName | 字符串 | 输入 | 自变量名（如 `x`） |
| YName | 字符串 | 输入 | 纵坐标名（如 `y`，隐式模型必填） |
| ParamNames | 字符串元组 | 输入 | 参数名元组 |
| ModelType | 字符串/整数 | 输入 | `explicit`（`y=f(x)`）/ `implicit`（`F(x,y)=0`），大小写不敏感，也接受 `0/1` |
| ResidualType | 字符串/整数 | 输入 | `geometric`（几何距离，推荐）/ `vertical`（垂直残差，仅显式），大小写不敏感，也接受 `0/1` |
| ModelHandle | 句柄 | 输出 | 模型句柄（`cv_geom_fit` 输入，`cv_fit_clear` 释放） |

建模时一次性完成表达式白名单校验、预编译与**最小采样数自动推导**——不再需要手工传 `MinSampleSize`。

### cv_geom_fit

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ModelHandle | 句柄 | 输入 | `cv_geom_create` 生成的句柄 |
| XData / YData | 实数元组 | 输入 | 观测点坐标，两者长度必须一致 |
| InitialValues | 实数元组 | 输入 | 参数初值（长度 = 参数数，可为空取 0） |
| Threshold | 实数 | 输入 | 内点几何距离阈值（须 > 0） |
| MaxIter | 整数 | 输入 | 最大迭代次数（须 > 0） |
| OutlierRatio | 实数 | 输入 | 外点比例估计 `[0,1)`，用于推算所需迭代次数 |
| Confidence | 实数 | 输入 | 置信度 `(0,1)`，`<= 0` 取 0.99 |
| Seed | 整数 | 输入 | 随机种子，`0` = 非确定性 |
| ParamValues | 实数元组 | 输出 | 拟合参数值（顺序同 `ParamNames`） |
| InlierMask | 整数元组 | 输出 | 内点掩码（1 = 内点，逐点输出，长度 = 点数） |
| ResidualSum | 实数 | 输出 | 内点（加权）残差平方和 |
| Iterations | 整数 | 输出 | 实际迭代次数 |
| Status | 整数 | 输出 | 状态码，**`0` = 成功**，见下表 |
| StatusMessage | 字符串 | 输出 | 状态文本（成功为 `Success`） |
| InlierRatio | 实数 | 输出 | 内点比例 `[0,1]` |

**Status 状态码**（`cv_flow/include/cvflow/ransac.hpp` 的 `RansacStatus`）：

| Status | 含义 | 结果可用性 |
|---|---|---|
| `0` | 成功 | 有效 |
| `-1` | 参数非法 | 未计算 |
| `-2` | 表达式解析失败 | 未计算 |
| `-3` | 表达式评估失败 | 未计算 |
| `-4` | 表达式求值预算耗尽（超时） | 最佳候选 |
| `-5` | 数据点不足（< 最小采样数） | 未计算 |
| `-6` | 采样持续退化 | 最佳候选 |
| `-7` | 求解持续失败 | 最佳候选 |
| `-8` | 数值异常 | 最佳候选 |
| `-9` | 未收敛（输出最佳候选） | 最佳候选 |
| `-10` | 达到最大迭代（输出最佳候选） | 最佳候选 |
| `-99` | 内部错误 | 未计算 |

> 数据点不足与参数非法属于**数据问题**：supply 会前置校验并写入 `Status`（`-5` / `-1`），**不**作为算子错误抛出。

**算子错误码**：

| 错误码 | 含义 |
|---|---|
| 30001 | `ModelHandle` 不是有效的几何模型句柄（类型不符或已 `cv_fit_clear`） |
| 10201 | `XData` 与 `YData` 长度不一致 |
| 30002 | 拟合执行异常 |
| 30005 | `ModelType` 非法（须 `explicit`/`implicit` 或 `0/1`） |
| 30006 | `ResidualType` 非法（须 `vertical`/`geometric` 或 `0/1`） |
| 30007 | 模型构建失败（具体原因由算子错误文本给出，如表达式白名单/语法问题） |

### 模型表达式

| 模型 | 类型 | 表达式 | 参数 |
|---|---|---|---|
| 直线 | 显式 | `"a*x+b"` | `["a","b"]` |
| 二次多项式 | 显式 | `"a*pow(x,2)+b*x+c"` | `["a","b","c"]` |
| 幂函数 | 显式 | `"a*pow(x,b)+c"` | `["a","b","c"]` |
| 圆 | 隐式 | `"pow(x-cx,2)+pow(y-cy,2)-r*r"` | `["cx","cy","r"]` |
| 椭圆 | 隐式 | `"pow((x-cx)/a,2)+pow((y-cy)/b,2)-1"` | `["cx","cy","a","b"]` |
| 一般圆锥 | 隐式 | `"A*x*x+B*x*y+C*y*y+D*x+E*y+F"` | `["A","B","C","D","E","F"]` |

- **几何残差**：隐式模型为 `|F(x,y)| / ||grad F(x,y)||`（数值梯度）；显式模型选 `geometric` 时为 `|y-f(x)| / sqrt(1+f'(x)^2)`。内点判定统一用几何距离与 `Threshold` 比较。
- **求解策略自动选择**：建模时判断表达式对参数是否线性——线性走 Eigen 最小二乘（隐式做 SVD 零空间解，自由度为 `nParams-1`），非线性走 Eigen LevenbergMarquardt（隐式用 Taubin 几何归一化残差）；求解持续失败会中断并输出最佳候选。
- **表达式安全**：表达式长度与求值预算均有上限，超限返回 `-4`（不穿越 supply）。
- 函数白名单：`sin/cos/tan/asin/acos/atan/atan2/sqrt/abs/exp/log/log10/log2/min/max/floor/ceil/sinh/cosh/tanh/sign/rint/pow`。

**示例**（显式二次多项式 + 含离群点的隐式圆）：

```hdevelop
* 显式：y = x^2 + x + 1
XData := [0,1,2,3,4]
YData := [1,3,7,13,21]
cv_geom_create ('a*pow(x,2)+b*x+c', 'x', 'y', ['a','b','c'], 'explicit', 'geometric', H0)
cv_geom_fit (H0, XData, YData, [0.5,0.5,0.5], 1.0, 1000, 0.2, 0.99, 0, \
             PV, Mask, RSS, Iter, Status, SM, Ratio)
cv_fit_clear (H0)
* → PV ≈ [1,1,1]，Status = 0，Ratio = 1

* 隐式圆：圆心 (2,3)、半径 5，并注入离群点
cv_geom_create ('pow(x-cx,2)+pow(y-cy,2)-r*r', 'x', 'y', ['cx','cy','r'], 'implicit', 'geometric', H1)
cv_geom_fit (H1, X2, Y2, [0,0,1], 0.5, 3000, 0.35, 0.99, 42, \
             PV2, Mask2, RSS2, Iter2, Status2, SM2, Ratio2)
cv_fit_clear (H1)
* → PV2 ≈ [2,3,5]
```

例程：`examples/cv_ransac_v4.hdev`（显式多项式 / 隐式圆 / 隐式直线三档）、`examples/cv_ransac_fit.hdev`

---

## 数学 / 矩阵扩展算子

本扩展包中的数学/矩阵类算子（定义于 `def/Halcon_Math.def`，实现于 `source/Halcon_Math.cpp`）。

- HALCON 版本：24.11
- 依赖：Eigen 5.x、Armadillo 14.x、muparser 2.3.x、C++17 标准库

### 数据模型约定

| 数据 | 承载方式 |
|---|---|
| 矩阵 | `real` 单通道图像，**height = 行数，width = 列数** |
| 向量 | `1×N` 或 `N×1` 的 `real` 图像 |
| 标量 / 索引 | tuple 输出（double / long） |

> 涉及矩阵/向量运算的算子**只支持 `real`（32 位浮点）**——因为结果含符号与小数，`byte`/`uint2` 无法表达。
> 内部一律以 `double` 计算，输出时转回 `float`。

---

### 矩阵分解与求解

#### eigen_svd

奇异值分解，对应 `Eigen::JacobiSVD`（满 U / V）。

```
eigen_svd(ImageA : ImageU, ImageS, ImageV : )
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | real 单通道矩阵 A（m×n） |
| ImageU | 图像 | 输出 | real 矩阵 U（m×m） |
| ImageS | 图像 | 输出 | real 向量 S（1×min(m,n)，**降序**） |
| ImageV | 图像 | 输出 | real 矩阵 V（n×n） |

满足 **A = U · diag(S) · Vᵀ**。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 real 单通道矩阵 |

例程：`examples/math/eigen_svd.hdev`

---

#### eigen_ldlt

解对称线性方程组 A·x = b，对应 `Eigen::LDLT`（要求 A 对称）。

```
eigen_ldlt(ImageA, ImageB : ImageX : )
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | real 方阵 A（n×n，须对称） |
| ImageB | 图像 | 输入 | real 矩阵 b（n×1 或 n×k，可多右端项） |
| ImageX | 图像 | 输出 | real 解 x（n×1 或 n×k） |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 real 单通道矩阵 |
| 30002 | A 不是方阵 |
| 30003 | B 的行数 ≠ A 的维数 |
| 30004 | 求解失败 |

例程：`examples/math/eigen_ldlt.hdev`

---

#### eigen_llt

解正定线性方程组 A·x = b，对应 `Eigen::LLT`（要求 A 正定）。

```
eigen_llt(ImageA, ImageB : ImageX : )
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | real 方阵 A（n×n，须正定） |
| ImageB | 图像 | 输入 | real 矩阵 b（n×1 或 n×k） |
| ImageX | 图像 | 输出 | real 解 x（n×1 或 n×k） |

错误码：同 `eigen_ldlt`。

例程：`examples/math/eigen_llt.hdev`

---

### 插值

#### arma_interp1

一维插值，对应 `arma::interp1`。

```
arma_interp1(ImageX, ImageY, ImageXI : ImageYI : InterpMethod)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageX | 图像 | 输入 | real 向量，采样点坐标（1×N） |
| ImageY | 图像 | 输入 | real 向量，采样点值（1×N，长度须与 X 一致） |
| ImageXI | 图像 | 输入 | real 向量，查询点（1×M） |
| ImageYI | 图像 | 输出 | real 向量，插值结果（1×M） |
| InterpMethod | 字符串 | 输入 | 插值方法，默认 `"linear"` |

`InterpMethod` 取值（⚠️ 实际仅支持下面两种类型，其余字符串会导致错误码 30003）：

| 值 | 说明 |
|---|---|
| `linear` | 线性插值（默认） |
| `nearest` | 最近邻插值 |
| `*linear` | 线性插值，且**假定 X 与 XI 均已单调递增**（跳过排序检查，更快） |
| `*nearest` | 最近邻插值，且假定 X 与 XI 单调递增 |

> ⚠️ **重要**：Armadillo 的 `interp1` **只比较方法名的前两个字符**
> （`l…` → linear，`n…` → nearest，`*l` → 线性单调，`*n` → 最近邻单调），
> 且方法名长度必须 **≥ 2 个字符**。因此：
> - `previous`、`pchip`、`cubic` 等名称**不被支持** → 返回 30003
> - `next` 会被**当作 `nearest`** 处理（不会报错，但语义不符）
> - 传空串或单字符（如 `"l"`）→ 返回 30003

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 非 real 单通道 |
| 30002 | X 与 Y 长度不一致 |
| 30003 | 插值失败 |

例程：`examples/math/arma_interp1.hdev`

---

### 表达式拟合（两步式）

拟合类算子统一采用 **create（建模）+ fit（拟合）** 两步式：表达式只在建模时解析一次，同一个模型句柄可反复拟合（批量 / 循环场景**零表达式解析开销**），句柄用 `cv_fit_clear` 显式释放。模型本体在 **cv_flow 静态库**（`cvflow::LmModel` / `cvflow::LinearModel` / RANSAC 句柄），supply 只做参数搬运。

| 模型 | 建模算子 | 拟合算子 | 适用 |
|---|---|---|---|
| LM 非线性最小二乘 | `cv_lm_create` | `cv_lm_fit` | 任意表达式（参数可非线性）；支持 M 个自变量、K 个输出（共享参数） |
| 线性最小二乘 | `cv_linear_create` | `cv_linear_fit` | 对参数线性（自变量可非线性）；1~2 个自变量，列主元 QR |
| RANSAC 几何拟合 | `cv_geom_create` | `cv_geom_fit` | 含离群点的几何拟合，见上节「RANSAC 扩展算子」 |

### 迁移对照（旧一步到位算子已移除）

| 旧算子（已删除） | 新用法 |
|---|---|
| `eigen_lm_fit` | `cv_lm_create` + `cv_lm_fit`（M=1、K=1） |
| `eigen_lm_fit_2d` | `cv_lm_create` + `cv_lm_fit`（M ≥ 1、K ≥ 1，同一算子覆盖，不再区分 `_2d`） |
| `linear_fit` | `cv_linear_create` + `cv_linear_fit`（`XNames` 传 1 个） |
| `linear_fit_2d` | `cv_linear_create` + `cv_linear_fit`（`XNames` 传 2 个，`X/Y` 为自变量、`Z` 为观测） |
| `cv_ransac_fit` | `cv_geom_create` + `cv_geom_fit` |

> 迁移要点：①表达式、参数名、自变量名改由 `*_create` 传入（建模时做查重 / 冲突 / 白名单校验）；②数据改为在 `*_fit` 传入；③模型类型 / 残差类型等选项在建模时确定；④句柄用完调 `cv_fit_clear` 立即释放原生资源。

### cv_lm_create

构建 LM 非线性拟合模型：一次性校验并编译表达式（muparser 整个生命周期只解析这一次）。

```
cv_lm_create(:: Expressions, ParamNames, XNames : ModelHandle)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Expressions | 字符串元组 | 输入 | 输出表达式元组（1~K 个，**共享同一组参数**，如畸变场 `['dx模型','dy模型']`） |
| ParamNames | 字符串元组 | 输入 | 共享参数名元组（建模时做查重 / 冲突校验） |
| XNames | 字符串元组 | 输入 | 自变量名元组（1~M 个，M=1 即一维拟合） |
| ModelHandle | 句柄 | 输出 | 拟合模型句柄（`cv_lm_fit` 输入，`cv_fit_clear` 释放） |

错误码（`30000 + errCode`）：

| 错误码 | 含义 |
|---|---|
| 30001 | 至少需要一个模型表达式 |
| 30002 | 至少需要一个自变量名 |
| 30003 | 至少需要一个参数名 |
| 30004 | 表达式为空串 |
| 30005 | 自变量名为空串 |
| 30006 | 自变量名重复 |
| 30007 | 参数名为空串 |
| 30008 | 参数名重复 |
| 30009 | 参数名与自变量名冲突 |
| 30010 | 内存不足 |

### cv_lm_fit

用模型句柄做 LM 非线性最小二乘拟合，底层为 `Eigen::LevenbergMarquardt`（MINPACK LM 的 Eigen 实现）+ `NumericalDiff` 数值微分；`ftol` / `xtol` 内部固定为 `1e-12`，梯度判据 `gtol` 置 0（关闭）。

```
cv_lm_fit(:: ModelHandle, X, Y, Z, InitialValues, MaxIter, Eps : ParamValues, RSS, Iterations, Status, StatusMessage)
```

**数据传递模式**（由建模时的 M / K 决定，务必对照下表）：

| 模式 | 自变量 | 观测 |
|---|---|---|
| K=1、M=1（单输出单自变量） | `X` | `Y`（`Z` 传空） |
| K=1、M=2 | `X`、`Y` 两个自变量 | `Z` |
| K>1（多输出共享参数） | `X` = 自变量拉平（点数 × M） | `Y` = 观测拉平（点数 × K）（`Z` 传空） |

> K=1 且 M>2 时不能走 X/Y/Z 模式（报 30022），请改用「拉平」模式：把 M 个自变量按行优先拉平到 `X`、观测放到 `Y`。

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ModelHandle | 句柄 | 输入 | `cv_lm_create` 生成的句柄 |
| X / Y / Z | 实数元组 | 输入 | 见上表「数据传递模式」 |
| InitialValues | 实数元组 | 输入 | 参数初值（长度 = 参数数，初值只需大致量级） |
| MaxIter | 整数 | 输入 | 最大函数求值次数，`<= 0` 取 `400*(nParams+1)` |
| Eps | 实数 | 输入 | 数值微分步长（`<= 0` 由 Eigen 自动选取） |
| ParamValues | 实数元组 | 输出 | 拟合参数值，顺序同 `ParamNames`（失败时回吐初值） |
| RSS | 实数 | 输出 | 残差平方和（非有限时会追加警告到 `StatusMessage`） |
| Iterations | 整数 | 输出 | LM 迭代次数 |
| Status | 整数 | 输出 | LM 状态码，见下表 |
| StatusMessage | 字符串 | 输出 | 状态文本 |

**残差定义**：$\;fvec_{h,k} = YData_{h,k} - \text{expr}_k(X_h;\ \mathbf{p})$，即最小化所有输出合并的 $\sum (YData - f(X))^2$。

**Status 状态码**：

| Status | 含义 | 结果可用性 |
|---|---|---|
| `1` | 相对残差下降满足 `ftol` | 已收敛 |
| `2` | 相邻迭代参数变化满足 `xtol` | 已收敛 |
| `3` | `ftol` 与 `xtol` 同时满足 | 已收敛 |
| `5` | 达到最大函数求值次数 | 可用但可能未到最优，可增大 `MaxIter` |
| `6` | `ftol` 过小，无法进一步下降 | 通常已到平台区，结合 `RSS` 判断 |
| `7` | `xtol` 过小，参数无法进一步改善 | 同上 |
| `0` | 输入参数不当 / 表达式解析或求值失败 | 见 `StatusMessage` |
| `4` / `8` | 梯度余弦判据相关 | **本实现 `gtol=0` 已关闭，不会出现** |
| `-1` / `-2` | 运行中 / 未开始 | 正常调用不会出现 |

> 判断是否成功不要只看 Status：`1~3` 为严格收敛；`5~7` 表示迭代正常结束但未达严格判据，应结合 `RSS` 与 `StatusMessage`。
> 表达式语法错误、求值定义域错误等运行期问题**不报算子错误**，而是写入 `StatusMessage`（文本以 `expression error:` 或 `error:` 开头）。

**算子错误码**：

| 错误码 | 含义 |
|---|---|
| 30001 | 句柄无效（类型不符或已 `cv_fit_clear`），**或** `InitialValues` 长度 ≠ 参数数（两者同码，看错误文本区分） |
| 30002 | 自变量长度非法（`X` 长度须为自变量个数 M 的整数倍） |
| 30003 | 观测长度 ≠ 点数 × 输出个数 K |
| 30004 | 欠定（点数 < 参数数） |
| 30021 | X/Y/Z 与模型维度不匹配（如 K=1,M=1 未传 `Y`） |
| 30022 | K=1 且 M>2 时使用了 X/Y/Z 模式（请改用拉平模式） |

**表达式语法**（由 muparser 提供）：

| 类别 | 可用内容 |
|---|---|
| 运算符 | `+ - * / ^`（`^` 为乘方）、一元 `-` |
| 常用函数 | `sin cos tan asin acos atan atan2 sinh cosh tanh exp log log2 log10 sqrt abs ceil floor pow` |
| 其它 | `min max sum avg if(cond,a,b) sign`，以及常量 `pi`、`e` |

**示例**：$y = a\,e^{-b x}$（真值 a=2, b=0.5），并演示句柄复用

```hdevelop
X := []
Y := []
for I := 0 to 19 by 1
  X := [X, 0.2 * I]
  Y := [Y, 2.0 * exp(-0.5 * 0.2 * I)]
endfor

cv_lm_create ('a*exp(-b*x)', ['a','b'], ['x'], LmHandle)
cv_lm_fit (LmHandle, X, Y, [], [1.0, 1.0], 2000, 0.0, Params, RSS, It, St, Msg)
* → Params ≈ [2.0, 0.5]，St ∈ {1,2,3}
cv_lm_fit (LmHandle, X, Y, [], [3.0, 0.8], 2000, 0.0, Params2, RSS2, It2, St2, Msg2)
* 句柄复用：换个相近初值再拟合，应收敛到同一结果
cv_fit_clear (LmHandle)
```

**多输出共享参数示例**（M 个自变量 + K 个输出的畸变场）：

```hdevelop
E1 := 'u*(k1*r2 + k2*r2^2) + p1*(r2 + 2*u^2) + 2*p2*u*v'
E2 := 'v*(k1*r2 + k2*r2^2) + 2*p1*u*v + p2*(r2 + 2*v^2)'
* XNames = [u, v, r2]，r2 = u^2+v^2 作为预计算派生量直接当一个自变量传入
cv_lm_create ([E1, E2], ['k1','k2','p1','p2'], ['u','v','r2'], LmDist)
* XData 每点 3 个值 [u, v, r2]（行优先拉平）；YData 每点 2 个值 [dx, dy]
cv_lm_fit (LmDist, XData, YData, [], [-0.1, 0.0, 0.0, 0.0], 0, 0.0, \
           ParamValues, Rss, Iterations, Status, StatusMessage)
cv_fit_clear (LmDist)
```

> 其它可用模型：`'a*x+b'`（线性，也可用 `cv_linear_*`）、`'a*pow(x,2)+b*x+c'`、`'a*exp(-b*x)+c'`、`'a*sin(b*x+c)+d'`、`'a*exp(-((x-b)/c)^2)'`、`'1/(1+exp(-a*(x-b)))'`。

> 模型的参数**可辨识性**由用户负责：例如 `a*exp(b*x)+c` 在数据几乎不衰减时 `a` 与 `c` 会相互抵消，可能出现 `RSS` 很小但参数跑偏的情况。

例程：`examples/math/eigen_lm_fit.hdev`、`examples/math/eigen_lm_fit_2d.hdev`（已迁移到两步式 API）、`examples/cv_fit_demo.hdev`

### cv_linear_create

构建线性最小二乘模型：一次性校验并编译表达式（只解析一次）。`XNames` 传 1 个 = 一维，传 2 个 = 二维。

```
cv_linear_create(:: Expression, ParamNames, XNames : ModelHandle)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Expression | 字符串 | 输入 | 模型表达式（单输出，**对参数必须线性**，自变量可非线性） |
| ParamNames | 字符串元组 | 输入 | 参数名元组（建模时做查重 / 冲突校验） |
| XNames | 字符串元组 | 输入 | 自变量名元组（1~2 个） |
| ModelHandle | 句柄 | 输出 | 模型句柄（`cv_linear_fit` 输入，`cv_fit_clear` 释放） |

错误码（`30000 + errCode`）：

| 错误码 | 含义 |
|---|---|
| 30001 | 表达式为空 |
| 30002 | 至少需要一个参数名 |
| 30003 | 自变量个数必须为 1 或 2 |
| 30004 | 参数名为空串 |
| 30005 | 参数名重复 |
| 30006 | 自变量名为空串 |
| 30007 | 自变量名重复 |
| 30008 | 参数名与自变量名冲突 |
| 30009 | 内存不足 |

### cv_linear_fit

用模型句柄做线性最小二乘拟合（设计矩阵列主元 QR + 对参数线性校验），无需初值、无需迭代。

```
cv_linear_fit(:: ModelHandle, X, Y, Z, Tolerance : Coefficients, RSS, Rank, Success, Message)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ModelHandle | 句柄 | 输入 | `cv_linear_create` 生成的句柄 |
| X / Y / Z | 实数元组 | 输入 | 一维：`X` = 自变量、`Y` = 观测（`Z` 传空）；二维：`X`、`Y` = 两个自变量、`Z` = 观测 |
| Tolerance | 实数 | 输入 | 线性校验容差，`<= 0` 取 `1e-10` |
| Coefficients | 实数元组 | 输出 | 拟合系数，顺序同 `ParamNames` |
| RSS | 实数 | 输出 | 残差平方和（失败为 -1） |
| Rank | 整数 | 输出 | 设计矩阵数值秩 |
| Success | 整数 | 输出 | 1 = 成功，0 = 失败 |
| Message | 字符串 | 输出 | 结果描述文本 |

> 模型对参数必须线性（如 `a + b*x + c*pow(x,2)`）；自变量可非线性（如 `a + b*exp(-x)`）。
> 参数非线性模型（如 `a*exp(b*x)+c`）请用 `cv_lm_create` + `cv_lm_fit`。

**算子错误码**：

| 错误码 | 含义 |
|---|---|
| 30001 | 句柄无效（类型不符或已 `cv_fit_clear`） |
| 30021 | 一维模型却传了非空 `Z` |
| 30022 | X/Y/Z 长度不匹配（或长度为空） |

**示例**（一维二次多项式，真值 1,2,3；二维见 `examples/cv_fit_demo.hdev`）：

```hdevelop
Xl := []
Yl := []
for I := 0 to 9 by 1
  Xl := [Xl, I]
  Yl := [Yl, 1 + 2*I + 3*I*I]
endfor

cv_linear_create ('a + b*x + c*pow(x,2)', ['a','b','c'], ['x'], LinH)
cv_linear_fit (LinH, Xl, Yl, [], 0.0, Coef, RSS, Rank, OK, Msg)
cv_fit_clear (LinH)
* → Coef ≈ [1,2,3]，OK = 1

* 二维：z = a + b*x + c*y（X、Y 为自变量，Z 为观测）
cv_linear_create ('a + b*x + c*y', ['a','b','c'], ['x','y'], LinH2)
cv_linear_fit (LinH2, X, Y, Z, 0.0, Coef2, RSS2, Rank2, OK2, Msg2)
cv_fit_clear (LinH2)
```

例程：`examples/math/linear_fit.hdev`、`examples/math/linear_fit_2d.hdev`、`examples/math/linear_fit_line.hdev`（`ax+by+c=0` 归一化用法 + 可视化）

### cv_fit_clear

显式释放拟合模型句柄占用的原生资源（表达式编译产物等）。

```
cv_fit_clear(:: ModelHandle :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ModelHandle | 句柄 | 输入 | `cv_lm_create` / `cv_linear_create` / `cv_geom_create` 生成的句柄 |

- 释放后句柄**不可再用于拟合**（再用会报 30001）；重复 `clear` 是安全的（幂等）。
- 模型内存立即释放，句柄壳由 HALCON 句柄 GC 回收。
- 句柄类型与算子匹配校验：把 linear 句柄传给 `cv_lm_fit`、或把 `cv_lm` 句柄传给 `cv_linear_fit` / `cv_geom_fit` 都会报 30001。

---

### 序列处理

#### std_nth_element

找第 n 小元素，对应 `std::nth_element`（比全排序更快）。

```
std_nth_element(Image : : NIndex : Value)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | real 单通道（展平后按行优先处理） |
| NIndex | 整数 | 输入 | 0-based 索引（0 = 最小值） |
| Value | 实数 | 输出 | 第 NIndex 小的元素值 |

> 典型用法：`NIndex = 总数/2` 取中位数，`NIndex = 0` 取最小值。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 real 单通道 |
| 30002 | 索引越界 |

例程：`examples/math/std_nth_element.hdev`

---

#### std_sort

序列排序，对应 `std::sort`。输入展平排序后按原尺寸 reshape 输出。

```
std_sort(Image : ImageOut : Descending)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | real 单通道 |
| ImageOut | 图像 | 输出 | real，与输入同尺寸（已排序） |
| Descending | 整数 | 输入 | 0 = 升序（默认），1 = 降序 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 real 单通道 |

例程：`examples/math/std_sort.hdev`

---

#### std_lower_bound

二分查找下界，对应 `std::lower_bound`（要求输入已升序排序）。

```
std_lower_bound(Image : : Value : Index)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | real 单通道（**须已升序**，可先用 `std_sort`） |
| Value | 实数 | 输入 | 查询值 |
| Index | 整数 | 输出 | 第一个 ≥ Value 的元素索引（0-based） |

> 若所有元素均 < Value，返回元素总个数（对应 std 的 `end` 位置）。

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入非 real 单通道 |

例程：`examples/math/std_lower_bound.hdev`

---

### 算子速查表

| 算子 | 输入类型 | 主要输出 | 备注 |
|---|---|---|---|
| `eigen_svd` | real 矩阵 | U / S / V 三个图像 | S 为降序行向量 |
| `eigen_ldlt` | real 方阵 + b | 解 x 图像 | A 须对称 |
| `eigen_llt` | real 方阵 + b | 解 x 图像 | A 须正定 |
| `arma_interp1` | 三个 real 向量 | 插值结果图像 | linear / nearest（可加 `*` 单调前缀） |
| `cv_lm_create` / `cv_lm_fit` | 表达式 + 数据 tuple | 参数值 / RSS / 状态 | LM 非线性；M 自变量 × K 输出共享参数，建模式（零解析开销） |
| `cv_linear_create` / `cv_linear_fit` | 表达式 + 数据 tuple | 系数 / RSS / 秩 / 状态 | 参数线性；1~2 自变量，列主元 QR |
| `cv_geom_create` / `cv_geom_fit` | 表达式 + 点数据 tuple | 参数值 / 内点掩码 / 状态 | RANSAC 几何拟合，含离群点 |
| `cv_fit_clear` | 拟合模型句柄 | — | 显式释放模型句柄（幂等） |
| `std_nth_element` | real 图像 | 标量 tuple | 0-based，可用于求中位数 |
| `std_sort` | real 图像 | 排序后图像 | 升/降序 |
| `std_lower_bound` | real 升序图像 | 索引 tuple | 返回 `end` = 元素个数 |

---

## 项目结构

```
Halcon_SoftwarePackage/
├── def/                  # HALCON 算子定义文件（按模块拆分）
├── doc/                  # HTML 参考文档（构建时生成）
├── examples/             # HDevelop 示例脚本
│   ├── sqlite.hdev
│   ├── modbus.hdev
│   ├── spdlog.hdev
│   ├── mysql.hdev
│   ├── Image2String.hdev
│   ├── opencv/           # OpenCV 算子例程
│   └── math/             # 数学 / 矩阵算子例程
├── cv_region/            # 独立 region 库（C++17；静态库 cvr_core 直接链入扩展包，可脱离 HALCON 调用）
├── cv_flow/              # 流程算法库（RANSAC / LM / 线性拟合、1D 测量、特征匹配、ROI 运算、图像序列化；静态库 cvf::cv_flow）
├── help/                 # 算子签名数据库（构建时同步，HALCON 调用校验依赖）
├── include/              # 头文件
├── source/               # 源代码
├── CMakeLists.txt        # CMake 构建配置（纯 vcpkg，跨平台）
├── vcpkg.json            # vcpkg 依赖清单
├── README.md             # 本文档（含全部算子参考）
├── .gitignore
├── .gitattributes
└── LICENSE               # GPL-3.0
```

## 环境要求

- **HALCON** SDK（需配置 `HALCONROOT` 和 `HALCONEXAMPLES` 环境变量）
- **CMake** >= 3.21
- **vcpkg** 包管理器（`VCPKG_ROOT` 环境变量）
- **Visual Studio** 2022（Windows x64 编译）或 Linux GCC

## 编译

**Windows**
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

**Linux / macOS**
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

编译产物输出到 `bin/`（Windows）或 `lib/<platform>/`（Linux/macOS）。首次会自动下载编译 vcpkg 依赖，后续秒级缓存。

Debug / Release 切换（多配置生成器）：

```bash
cmake --build build --config Release   # 或 Debug
```

## 脱离 HALCON 复用核心库

扩展包的算法本体是 `cv_region/`（`cvr_core`，纯 STL region 运算）与 `cv_flow/`
（`cv_flow`，RANSAC 拟合 / 1D 测量 / 特征匹配 / LM 拟合等流程算法）两个静态库，
**不依赖 HALCON**，扩展包 supply 层只是它们的薄封装。其他 C++ 项目可直接复用：

```bash
# 1) 安装到任意前缀（库 + 头文件 + CMake package config）
cmake --install build --config Release --prefix D:/swpkg

# 2) 消费方 CMakeLists.txt
find_package(cvr_core CONFIG REQUIRED)   # cvr::cvr_core（纯 STL，零依赖）
find_package(cv_flow  CONFIG REQUIRED)   # cvf::cv_flow（自动解析 Eigen3/muparser/OpenCV）
target_link_libraries(app PRIVATE cvr::cvr_core cvf::cv_flow)

# 3) 配置消费方（把安装前缀和 vcpkg 依赖树加进前缀路径）
cmake -B build -DCMAKE_PREFIX_PATH="D:/swpkg;<本仓库>/build/vcpkg_installed/x64-windows"
```

代码里 `#include <cvr/cvr.hpp>` / `#include <cvflow/ransac.hpp>` 等即可直接调用，
两个库各自 `tests/` 目录下的测试程序就是脱离 HALCON 使用的范例。

## 部署与使用

### 1. 编译

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

产物：`bin/Halcon_SoftwarePackage.dll`（+ `c`/`cpp`/`dotnet` 接口变体）及全部 vcpkg 运行库（OpenCV、muparser、spdlog 等，POST_BUILD 自动拷贝）。

### 2. 包目录结构（部署依据）

HALCON 要求扩展包目录**同时包含**以下子目录（缺一不可，否则算子加载不到或签名校验失败）：

```
PackageDir/                  # HALCONEXTENSIONS 指向这里（部署态自取目录名）
├── bin/                     # 全部 .dll（含 vcpkg 运行库）
├── help/                    # operators_en_US.* 算子签名数据库（调用校验依赖，见常见问题 3）
└── doc/html/                # 算子帮助页（HDevelop 按 F1 查看，可选但强烈建议）
```

**开发态**：直接指向本仓库根目录即可（仓库自带 bin/ + help/ + doc/html/）。
**部署态**：把 `bin/`、`help/`、`doc/` 三个目录原样拷贝到目标机器同一文件夹。

### 3. 设置环境变量

**必需：`HALCONEXTENSIONS`** —— HALCON 扩展包搜索路径列表。

- **Windows 必须是反斜杠路径**（正斜杠会导致包加载失败）；多路径用分号 `;` 分隔
- 值为包目录（含 bin/ 的那一层），不是 bin 本身

方式一（GUI）：系统属性 → 高级 → 环境变量 → 新建用户变量：

```
变量名:  HALCONEXTENSIONS
变量值:  D:\desk\source\Halcon_Extension\Halcon_SoftwarePackage
```

方式二（PowerShell，立即写入用户注册表）：

```powershell
[Environment]::SetEnvironmentVariable("HALCONEXTENSIONS", "D:\desk\source\Halcon_Extension\Halcon_SoftwarePackage", "User")
```

前置条件：`HALCONROOT` 已在系统中配置好（HALCON 安装器默认完成）。

**改完环境变量必须重开终端 / HDevelop 才生效**（进程启动时读一次）。

### 4. 验证

```powershell
# 新开的终端中：
hrun -v examples\cv_region.hdev      # 跑通即部署成功（退出码 0）
```

或在 HDevelop 中直接调用任意 cv_ 算子；按 F1 可查看带完整中文参数说明的帮助页。

### 5. 常见问题

| 现象 | 排查 |
|---|---|
| HDevelop 里算子名找不到（红色） | `HALCONEXTENSIONS` 路径是否反斜杠、是否指到含 bin/ 的那层、是否重开了 HDevelop |
| 调用算子无反应、输出空、不报错 | help/ 签名库过期（多见于自编译更新后）：整包 `cmake --build build --config Release` 重建，或手动 `Copy-Item build\help\operators_en_US.* help\ -Force` |
| 加载报缺 DLL | bin/ 不完整：POST_BUILD 应从 vcpkg 自动拷贝全部运行库，确认 bin/ 下 OpenCV/muparser 等 dll 存在 |
| LNK1104 编译时 DLL 被占用 | 关掉 HDevelop/hrun 进程再编译 |

## 第三方依赖

全部由 vcpkg 管理，构建时自动下载编译。

| 库 | 说明 | vcpkg 包 |
|----|------|----------|
| [SQLite3](https://www.sqlite.org/) | 嵌入式数据库 | `sqlite3` |
| [libmodbus](https://libmodbus.org/) | Modbus 协议库 | `libmodbus` |
| [spdlog](https://github.com/gabime/spdlog) | 高性能 C++ 日志库 | `spdlog` |
| [MySQL](https://dev.mysql.com/) | MySQL C 客户端库 | `libmysql` |
| [OpenCV](https://opencv.org/) | 计算机视觉与图像处理 | `opencv4` |
| [Exiv2](https://exiv2.org/) | 图像 EXIF 元数据读写 | `exiv2` |
| [Eigen](https://eigen.tuxfamily.org/) | 线性代数模板库（SVD / LDLT / LLT / LM） | `eigen3` |
| [Armadillo](https://arma.sourceforge.net/) | C++ 线性代数与统计库 | `armadillo` |
| [muparser](https://beltoforion.de/en/muparser/) | 数学表达式解析（`cv_lm_create` / `cv_geom_create` 的运行时模型） | `muparser` |

## 许可证

本项目基于 [GPL-3.0](LICENSE) 许可证开源。

## 句柄 TAG 分配表

用户句柄类型（`HHandleInfo` 的第一个参数，即 `include/Halcon_Def.h` 里的 `H_<模块>_TAG`）的编号池，**公司内各扩展包共用**。新增句柄类型前必须先在此登记领一个空号，禁止撞号——`HGetCElemH1` 按 TAG 校验句柄类型，撞号后 A 模块的句柄会被 B 模块的算子接受，直接野指针崩溃。

### 0xC0FFEE 区段（本包主用）

| TAG | 用途 | 状态 |
|---|---|---|
| 0xC0FFEE10 | python | 已占用 |
| 0xC0FFEE20 | 海康相机 | 已占用 |
| 0xC0FFEE30 | 海康采集卡 | 已占用 |
| 0xC0FFEE31 | 埃克采集卡 | 已占用 |
| 0xC0FFEE40 | sqlite | 已占用（Halcon_Def.h） |
| 0xC0FFEE50 | UI（IUP） | 已占用 |
| 0xC0FFEE60 | BV相机 | 已占用 |
| 0xC0FFEE70 | 自研AIcpu | 已占用 |
| 0xC0FFEE80 | 大恒相机 | 已占用 |
| 0xC0FFEE90 | 海康读码器 | 已占用 |
| 0xC0FFEEA0 | Spdlog | 已占用（Halcon_Def.h） |
| 0xC0FFEEB0 | MYSQL | 已占用（Halcon_Def.h） |
| 0xC0FFEEC0 | Modbus | 已占用（Halcon_Def.h） |
| 0xC0FFEED0 | 拟合模型（FitModel，LM/Linear/Geom 三族两步式拟合共用） | 已占用（Halcon_Def.h） |
| 0xC0FFEEE0 | — | 空 |
| 0xC0FFEEF0 | — | 空 |
| 0xC0FFEF00 | — | 空 |

### 备用区段（全空，主区段用满后再启用）

| 区段 | 可用编号 |
|---|---|
| 0xDEADBE | 0xDEADBE10–0xDEADBE80（步进 0x10） |
| 0xBAADF00D | 0xBAADF00D、0xBAADF10D–0xBAADF70D |
| 0xCAFEBABE | 0xCAFEBABE、0xCAFEBA10–0xCAFEBA70 |
| 0xFEEDFACE | 0xFEEDFACE、0xFEEDF10E–0xFEEDF70E |
