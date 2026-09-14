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
cv_blur(Image : ImageOut : Kwidth, Kheight)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageOut | 图像 | 输出 | 与输入同类型同尺寸 |
| Kwidth | 整数 | 输入 | 滤波核宽度，≥1 的奇数，默认 3 |
| Kheight | 整数 | 输入 | 滤波核高度，≥1 的奇数，默认 3 |

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

图像卷积，对应 `cv::filter2D`，卷积核通过 `real` 单通道图像传入（锚点默认为核中心，`ddepth=-1`，边界 `BORDER_DEFAULT`）。

```
cv_filter2d(Image, Kernel : ImageOut : :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| Kernel | 图像 | 输入 | 卷积核，必须为 `real` 单通道，任意宽高 |
| ImageOut | 图像 | 输出 | 与输入同类型同尺寸 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | Kernel 非 `real` 单通道 |
| 30002 | 输入图像类型不支持 |
| 30003 | OpenCV 执行异常 |

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

#### cv_subtract

图像逐元素相减（饱和截断），对应 `cv::subtract`。

```
cv_subtract(ImageA, ImageB : ImageOut : :)
```

`ImageOut = saturate(ImageA - ImageB)`（整数类型负值截断为 0）。

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | 单通道 `byte` / `uint2` / `real` |
| ImageB | 图像 | 输入 | 与 ImageA 同类型同尺寸 |
| ImageOut | 图像 | 输出 | 与输入同类型同尺寸 |

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 输入图像类型不支持 |
| 30002 | 两图类型不一致 |
| 30003 | 两图尺寸不一致 |
| 30004 | OpenCV 执行异常 |

---

#### cv_mat_mul

两个 16 位单通道图像做矩阵乘法，对应 `cv::gemm`。

```
cv_mat_mul(ImageA, ImageB : Outimage : :)
```

`C = A * B`，其中 `A: m×k`，`B: k×n`，结果 `C: m×n`。

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| ImageA | 图像 | 输入 | 16 位单通道（m×k） |
| ImageB | 图像 | 输入 | 16 位单通道（k×n） |
| Outimage | 图像 | 输出 | 16 位单通道（m×n） |

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

复刻 HALCON `measure_pos`（一维边缘测量，OpenCV 实现）。

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
| KeypointsRow | 元组 | 输出 | 特征点行坐标 |
| KeypointsCol | 元组 | 输出 | 特征点列坐标 |
| NumKeypoints | 元组 | 输出 | 特征点数量 |
| Descriptors | 图像 | 输出 | 描述子（byte） |
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
| HomMat2D | 元组 | 输出 | 仿射矩阵（2×3 展开） |
| Success | 元组 | 输出 | 是否成功 |
| InlierCount | 元组 | 输出 | 内点数量 |
| TranslateRow / TranslateCol | 元组 | 输出 | 平移量 |
| Angle | 元组 | 输出 | 旋转角度 |
| Scale | 元组 | 输出 | 缩放 |

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
cv_estimate_affine_2d(:: MethodId, RansacThreshold, SrcRow, SrcCol, DstRow, DstCol : HomMat2D, Success, InlierCount)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| MethodId | 整数 | 输入 | `RANSAC=8`（默认）/ `LMEDS=4` |
| RansacThreshold | 实数 | 输入 | 重投影阈值，默认 3.0 |
| SrcRow / SrcCol | 实数元组 | 输入 | 源点行 / 列坐标 |
| DstRow / DstCol | 实数元组 | 输入 | 目标点行 / 列坐标 |
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
cv_match_template(Image, TemplateImage : Result : :)
```

| 参数 | 类型 | 方向 | 说明 |
|---|---|---|---|
| Image | 图像 | 输入 | 单通道 `byte` / `real`（OpenCV 限制，不支持 16 位） |
| TemplateImage | 图像 | 输入 | 与 Image 同类型，尺寸 ≤ Image |
| Result | 图像 | 输出 | real 得分图，尺寸 (W−w+1)×(H−h+1) |

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
cv_kmeans(Samples : Labels, Centers : K, Attempts, TermEps, TermMaxIter)
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

错误码：

| 错误码 | 含义 |
|---|---|
| 30001 | 样本矩阵非 real |
| 30002 | K 非法（≤0 或 ＞ 样本数） |
| 30003 | OpenCV 执行异常 |

---

### 算子支持类型速查表

| 算子 | byte | uint2 | real | 多通道 |
|---|---|---|---|---|
| cv_blur | ✅ | ✅ | ✅ | ❌ |
| cv_median_blur | ✅ | ✅(3/5) | ✅(3/5) | ❌ |
| cv_filter2d | ✅ | ✅ | ✅ | ❌ |
| CLAHE_image | ✅ | ✅ | ❌ | ❌ |
| cv_add_weighted | ✅ | ✅ | ✅ | ❌ |
| cv_subtract | ✅ | ✅ | ✅ | ❌ |
| cv_mat_mul | ❌ | ✅ | ❌ | ❌ |
| ROI 算术 | ❌ | ✅ | ❌ | ❌ |
| cv_reshape | ✅ | ✅ | ✅ | ❌ |
| remap | ❌ | ✅ | ❌ | ❌ |
| cv_measure_pos | ✅ | ✅ | ✅ | ❌ |
| cv_orb_detect | ✅ | ❌ | ❌ | ❌ |
| cv_akaze_detect | ✅ | ❌ | ❌ | ❌ |
| cv_write_image | ✅ | ✅ | ❌ | ✅(3) |
| PNGIn | ✅ | ✅ | ❌ | ✅(3) |
| PNGOut | ✅ | ✅ | ❌ | ✅(3) |
| cv_solve | ❌ | ❌ | ✅ | ❌ |
| cv_estimate_affine_2d | — | — | — | — |
| cv_threshold_triangle | ✅ | ❌ | ❌ | ❌ |
| cv_calc_hist | ✅ | ✅ | ✅ | ❌ |
| cv_match_template | ✅ | ❌ | ✅ | ❌ |
| cv_kmeans | ❌ | ❌ | ✅ | ❌ |
| cv_multi_frame_median | ❌ | ❌ | ✅ | ❌ |

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

### 非线性拟合

#### eigen_lm_fit

通用**非线性最小二乘曲线拟合**，底层为 `Eigen::LevenbergMarquardt`（MINPACK LM 算法的 Eigen 实现）+ `NumericalDiff` 数值微分。

模型函数**不是编译期固定的**，而是在调用时以**字符串表达式**给出，由 `muparser` 在运行时解析。因此同一个算子可以拟合任意形式的模型（指数、幂、高斯、多参数有理式……），无需为每种模型重新编译扩展包。

```hdevelop
eigen_lm_fit (ModelExpression, ParamNames, InitialValues, XData, YData, XName, MaxIter, Eps \
              : ParamValues, Rss, Iterations, Status, StatusMessage)
```

**参数**（全部为 tuple，不涉及图像对象）：

| 参数 | 类型 | 说明 |
|---|---|---|
| `ModelExpression` | input_control, string | 模型表达式，如 `'a*exp(b*x)+c'`。式中出现的自变量名与参数名需与 `XName` / `ParamNames` 一致 |
| `ParamNames` | input_control, string tuple | 待拟合参数名，如 `['a','b','c']`。名称必须唯一且不能与 `XName` 相同 |
| `InitialValues` | input_control, real tuple | 参数初值，长度须与 `ParamNames` 相同（初值只需大致量级） |
| `XData` | input_control, real tuple | 自变量观测值 |
| `YData` | input_control, real tuple | 因变量观测值，长度须与 `XData` 相同 |
| `XName` | input_control, string | 自变量名，空串时取 `'x'` |
| `MaxIter` | input_control, integer | 最大函数求值次数，`<= 0` 时取 `400*(nParams+1)` |
| `Eps` | input_control, real | 数值微分步长 `epsfcn`，`<= 0` 时由 Eigen 自动取 `sqrt(machine eps)` |
| `ParamValues` | output_control, real tuple | 拟合后的参数值，顺序与 `ParamNames` 一致 |
| `Rss` | output_control, real | 残差平方和（表达式出错时为 `-1`） |
| `Iterations` | output_control, integer | 迭代次数 |
| `Status` | output_control, integer | LM 状态码，见下表 |
| `StatusMessage` | output_control, string | 状态文本说明 |

**残差定义**：$\;fvec_i = YData_i - f(XData_i;\ \mathbf{p})$，即最小化 $\sum_i \left(YData_i - f(XData_i)\right)^2$。

**Status 状态码**：

| Status | 含义 | 结果可用性 |
|---|---|---|
| `1` | 相对残差下降量满足 `ftol` | ✅ 已收敛 |
| `2` | 相邻两次参数变化满足 `xtol` | ✅ 已收敛 |
| `3` | `ftol` 与 `xtol` 同时满足 | ✅ 已收敛 |
| `4` | 梯度余弦判据满足 `gtol` | ✅ 已收敛 |
| `5` | 达到最大函数求值次数 | ⚠️ 结果可用但可能未到最优，可增大 `MaxIter` |
| `6` | `ftol` 过小，无法进一步下降 | ⚠️ 通常已到平台区，检查 `Rss` |
| `7` | `xtol` 过小，参数无法进一步改善 | ⚠️ 同上 |
| `8` | `gtol` 过小，无法进一步改善 | ⚠️ 同上 |
| `0` | 输入参数不当 / 表达式解析或求值失败 | ❌ 见 `StatusMessage` |
| `-1` / `-2` | 运行中 / 未开始（正常调用不会出现） | — |

> 拟合是否成功**不应只看 Status**：`1~4` 为严格收敛；`5~8` 表示迭代正常结束但未达严格判据，
> 此时应结合 `Rss` 与 `StatusMessage` 判断结果是否可接受。

**出错处理**：以下情况不会中断程序，而是返回 `Status = 0`、`Rss = -1`，原因写入 `StatusMessage`（以 `expression error:` 开头）：

- 表达式中出现未定义变量或语法错误
- 表达式求值失败（除零、`log` 负数、`sqrt` 负数等定义域错误）
- 拟合过程中出现非有限数

以下情况属于调用方参数错误，直接返回错误码（HALCON 报错）：

| 错误码 | 含义 |
|---|---|
| 30001 | 表达式为空 |
| 30002 | `ParamNames` 为空（至少需要一个参数） |
| 30003 | `InitialValues` 长度与 `ParamNames` 不一致 |
| 30004 | `XData` 与 `YData` 长度不一致 |
| 30005 | 数据点少于参数个数（欠定，需 `len(XData) >= len(ParamNames)`） |
| 30006 | 参数名为空串 |
| 30007 | 参数名与自变量名（`XName`）冲突 |
| 30008 | 参数名重复 |

**表达式语法**（由 muparser 提供）：

| 类别 | 可用内容 |
|---|---|
| 运算符 | `+ - * / ^`（`^` 为乘方）、一元 `-` |
| 常用函数 | `sin cos tan asin acos atan atan2 sinh cosh tanh exp log log2 log10 sqrt abs ceil floor` |
| 其它 | `min max sum avg if(cond,a,b) sign`，以及常量 `pi`、`e` |

**示例**：拟合 $y = a\,e^{b x} + c$

```hdevelop
XData := [0.0, 0.5, 1.0, 1.5, 2.0, 2.5]
YData := [4.62, 3.72, 3.05, 2.54, 2.14, 1.85]

eigen_lm_fit ('a*exp(b*x)+c', ['a','b','c'], [1.0, -0.1, 0.0], XData, YData, 'x', 0, 0.0, \
              ParamValues, Rss, Iterations, Status, StatusMessage)
* → ParamValues ≈ [3.50, -0.45, 1.20], Status ∈ {1,2,3,4}
```

**其它可用模型示例**：

| 表达式 | 模型 |
|---|---|
| `'a*x+b'` | 线性（亦可直接用 `eigen_ldlt`/`eigen_llt`） |
| `'a*x^2+b*x+c'` | 二次多项式 |
| `'a*exp(-b*x)+c'` | 指数衰减（初值 `b > 0`） |
| `'a*sin(b*x+c)+d'` | 正弦 |
| `'a*exp(-((x-b)/c)^2)'` | 高斯峰 |
| `'1/(1+exp(-a*(x-b)))'` | Logistic（无显式幅度） |

> ⚠️ 模型的参数**可辨识性**由用户负责：例如 `a*exp(b*x)+c` 中若数据几乎不衰减，
> `a` 与 `c` 会相互抵消而无法唯一确定，此时可能出现 `Rss` 很小但参数跑偏的情况。

例程：`examples/math/eigen_lm_fit.hdev`

#### eigen_lm_fit_2d

多维自变量 / 多输出的**联合**拟合。与 `eigen_lm_fit` 的区别：

| | `eigen_lm_fit` | `eigen_lm_fit_2d` |
|---|---|---|
| 自变量个数 | 1 | **任意**（典型 2 个：u, v） |
| 输出表达式个数 | 1 | **任意**（典型 2 个：dx, dy） |
| 参数组织 | 单个模型独享 | **所有表达式共享同一组参数** |

参数共享是关键——畸变场这类模型里 `dx` 与 `dy` 用的是同一组畸变系数，
必须联合求解，两个方向的信息才能互相约束。

```
eigen_lm_fit_2d(Expressions, ParamNames, InitialValues, XNames, XData, YData, MaxIter, Eps
                : ParamValues, Rss, Iterations, Status, StatusMessage)
```

| 参数 | 类型 | 说明 |
|---|---|---|
| Expressions | 字符串元组 | 一个或多个模型表达式，如 `['dx模型','dy模型']`，全部共享 `ParamNames` |
| ParamNames | 字符串元组 | 共享参数名，如 `['k1','k2','p1','p2']` |
| InitialValues | real 元组 | 参数初值，长度须与 `ParamNames` 一致 |
| XNames | 字符串元组 | 自变量名，如 `['u','v','r2']`。**预计算的派生量也可当作自变量传入**（如 $r^2=u^2+v^2$） |
| XData | real 元组 | 自变量数据，长度 = 点数 × M，**行优先**（每点的 M 个值连续存放） |
| YData | real 元组 | 观测数据，长度 = 点数 × K，**行优先**（每点的 K 个值连续存放） |
| MaxIter / Eps | 整数 / real | 同 `eigen_lm_fit` |
| ParamValues | real 元组 | 拟合参数，顺序同 `ParamNames` |
| Rss | real | **所有输出合并**的残差平方和（表达式出错时为 -1） |
| Iterations / Status / StatusMessage | — | 同 `eigen_lm_fit`（状态码含义一致） |

残差定义为 $fvec_{h,k} = YData_{h,k} - \text{expr}_k(X_h;\ \mathbf{p})$，向量长度 $H \times K$，
交由同一个 LevenbergMarquardt 联合最小化。

**错误码**：

| 错误码 | 含义 |
|---|---|
| 30001 / 30002 / 30003 | `Expressions` / `XNames` / `ParamNames` 为空 |
| 30004 | `InitialValues` 长度与 `ParamNames` 不符 |
| 30005 | `XData` 长度不是自变量个数的整数倍 |
| 30006 | `YData` 长度 ≠ 点数 × 表达式个数 |
| 30007 | 数据点少于参数个数（欠定） |
| 30008 / 30009 / 30011 | 表达式 / 自变量名 / 参数名为空串 |
| 30010 / 30012 | 自变量名 / 参数名重复 |
| 30013 | 参数名与自变量名冲突 |

**示例**（畸变场，dx/dy 共享 $k_1,k_2,p_1,p_2$）：

```hdevelop
E1 := 'u*(k1*r2 + k2*r2^2) + p1*(r2 + 2*u^2) + 2*p2*u*v'
E2 := 'v*(k1*r2 + k2*r2^2) + 2*p1*u*v + p2*(r2 + 2*v^2)'
* XData 每点 3 个值 [u, v, r2]；YData 每点 2 个值 [dx, dy]
eigen_lm_fit_2d ([E1, E2], ['k1','k2','p1','p2'], [-0.1, 0.0, 0.0, 0.0], \
                 ['u','v','r2'], XData, YData, 0, 0.0, \
                 ParamValues, Rss, Iterations, Status, StatusMessage)
```

> ⚠️ 名称中的 `2d` 指典型用途（二维定位 / 位移场拟合）。实现上自变量与输出个数
> **均不设上限**；`K = 1` 时它就是普通的二维曲面拟合。

> 💡 **该用哪个**：若模型对参数是**线性**的（经典径向+切向畸变、任意维多项式位移场
> 都属于此类），用 `cv_solve` 解线性最小二乘更合适——无需初值、无局部极小、一次求解。
> `eigen_lm_fit_2d` 的价值在参数**非线性**进入模型时，例如主点 $c_x, c_y$ 藏在
> $r^2 = (x-c_x)^2 + (y-c_y)^2$ 内部、需要与内参一起估计的场合。

例程：`examples/math/eigen_lm_fit_2d.hdev`

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
| `eigen_lm_fit` | 8 个 tuple（表达式 + 数据） | 参数值 / RSS / 状态 | 通用表达式拟合，无需重编译 |
| `eigen_lm_fit_2d` | 8 个 tuple（多表达式 + 数据） | 参数值 / RSS / 状态 | 多自变量 + 多输出共享参数，联合拟合 |
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
cmake --build build --config Debug
```

**Linux / macOS**
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

编译产物输出到 `bin/`（Windows）或 `lib/<platform>/`（Linux/macOS）。首次会自动下载编译 vcpkg 依赖，后续秒级缓存。

## 安装与使用

1. 编译项目，产物在 `bin/`（Windows）或 `lib/<platform>/`（Linux/macOS）
2. 将编译输出目录下所有 `.dll` / `.so` / `.dylib` 复制到同一文件夹
3. 添加系统环境变量 `HALCONEXTENSIONS`，值设为该文件夹路径
4. 在 HDevelop 中即可直接调用扩展算子

### 示例：SQLite

```
sqlite3_open (':memory:', SQLHandle)
sqlite3_exec (SQLHandle, 'CREATE TABLE test (id INTEGER, name TEXT)', ErrMsg)
sqlite3_exec (SQLHandle, 'INSERT INTO test VALUES (1, "hello")', ErrMsg)
sqlite3_get_table (SQLHandle, 'SELECT * FROM test', Names, Table, Rows, Cols, ErrMsg)
sqlite3_close (SQLHandle)
```

### 示例：spdlog 日志

```
* （可选）自定义异步线程池参数
spdlog_init_thread_pool (16384, 2)

* 创建滚动文件日志（5MB/文件，最多保留3个，异步写入）
spdlog_rotating_logger_mt ('app', 'D:/logs/app.log', 5242880, 3, LogHandle)

* 设置级别为 debug，输出 debug 及以上的日志
spdlog_set_level (LogHandle, 1)

* 设置自定义格式
spdlog_set_pattern (LogHandle, '[%Y-%m-%d %H:%M:%S.%e] [%l] %v')

* 记录不同级别日志（异步入队，调用立即返回）
spdlog_info (LogHandle, '程序启动完成')
spdlog_warn (LogHandle, '检测到配置缺失，使用默认值')
spdlog_err (LogHandle, '相机连接超时')

* 确保日志写入文件
spdlog_flush (LogHandle)

* 程序结束时关闭日志系统（等待队列排空后释放）
spdlog_shutdown ()
```

### 示例：Modbus TCP

```
modbus_tcp_connect ('127.0.0.1', 502, Handle)
modbus_set_slave_ID (Handle, 1)
modbus_write_bit (Handle, 0, 1)
modbus_read_bits (Handle, 0, 8, Bits)
modbus_read_registers (Handle, 0, 1, Registers)
modbus_write_register_float (Handle, 0, 3.14, 'abcd')
modbus_close (Handle)
```

### 示例：MySQL

```
* 连接 MySQL 服务器
mysql_real_connect ('127.0.0.1', 'root', '123456', 'test', 3306, '', 0, Handle, ErrMsg)

* 创建表并插入数据
mysql_query (Handle, 'CREATE TABLE IF NOT EXISTS test (id INT, name VARCHAR(50))', Status1)
mysql_query (Handle, 'INSERT INTO test VALUES (1, "halcon")', Status2)

* 查询数据
mysql_query (Handle, 'SELECT * FROM test', Status3)
mysql_store_result (Handle, Result)

* 断开连接（释放 Handle 自动关闭）
```

### 示例：OpenCV

```
* ORB 特征检测与匹配
cv_orb_detect (Image1, KeyPoints1, Descriptors1)
cv_orb_detect (Image2, KeyPoints2, Descriptors2)
cv_bf_knn_match (Descriptors1, Descriptors2, Matches, GoodMatches)
cv_estimate_affine_partial2d (KeyPoints1, KeyPoints2, GoodMatches, AffineMatrix)

* CLAHE 图像增强
CLAHE_image (InputImage, EnhancedImage, 2.0, 8)

* 矩阵乘法（两个 16 位单通道图像）
cv_mat_mul (ImageA, ImageB, ResultImage)

* ROI 叠加小图到大图
add_roi (SmallImage, BigImage, 100, 200, 300, 400)

* PNG 编解码（保留透明通道）
PNGIn (InputImage, PNGImage, 1)
PNGOut (PNGImage, OutputImage)

* 写入 EXIF 元数据
write_image_exif (Image, DictHandle)
```

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
| [muparser](https://beltoforion.de/en/muparser/) | 数学表达式解析（`eigen_lm_fit` 运行时模型） | `muparser` |

## 许可证

本项目基于 [GPL-3.0](LICENSE) 许可证开源。
0xC0FFEE10 python  0xC0FFEE20 海康相机   0xC0FFEE30 海康采集卡  0xC0FFEE31埃克采集卡  0xC0FFEE40 sqlite
0xC0FFEE50 UI   0xC0FFEE60BV相机   0xC0FFEE70自研AIcpu   0xC0FFEE80大恒相机
0xC0FFEE90海康读码器   0xC0FFEEA0Spdlog   0xC0FFEEB0MYSQL   0xC0FFEEC0
0xC0FFEED0   0xC0FFEEE0   0xC0FFEEF0   0xC0FFEF00

0xDEADBE10   0xDEADBE20   0xDEADBE30   0xDEADBE40
0xDEADBE50   0xDEADBE60   0xDEADBE70   0xDEADBE80

0xBAADF00D   0xBAADF10D   0xBAADF20D   0xBAADF30D
0xBAADF40D   0xBAADF50D   0xBAADF60D   0xBAADF70D

0xCAFEBABE   0xCAFEBA10   0xCAFEBA20   0xCAFEBA30
0xCAFEBA40   0xCAFEBA50   0xCAFEBA60   0xCAFEBA70

0xFEEDFACE   0xFEEDF10E   0xFEEDF20E   0xFEEDF30E
0xFEEDF40E   0xFEEDF50E   0xFEEDF60E   0xFEEDF70E