# Halcon_SoftwarePackage

HALCON Extension Package -- 为 [MVTec HALCON](https://www.mvtec.com/products/halcon) 机器视觉框架提供 **OpenCV 图像处理**、**SQLite3 数据库**、**Modbus 工业通信**、**spdlog 日志** 和 **MySQL 数据库** 扩展能力，在 HDevelop / HALCON 脚本中即可直接调用。

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

基于 [spdlog](https://github.com/gabime/spdlog) 高性能 C++ 日志库封装，支持多种日志输出方式和灵活的格式配置。

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

### OpenCV & Exiv2 图像处理

基于 OpenCV 和 Exiv2 库封装，提供图像几何变换、ROI 运算、特征检测匹配及 EXIF 元数据读写。

**图像变换**

| 算子 | 说明 |
|------|------|
| `remap` | 字典驱动的通用图像重映射（缩放/旋转/透视） |
| `PNGIn` / `PNGOut` | PNG 图像编解码（保留 Alpha 通道） |
| `CLAHE_image` | 自适应直方图均衡化，增强图像对比度 |

| `cv_write_image` | 保存图像为 PNG 文件（支持 8/16 位、单/三通道） |

**ROI 运算**

| 算子 | 说明 |
|------|------|
| `add_roi` | 将小图按 ROI 叠加到大图上 |
| `mul_roi` | 将小图按 ROI 乘到大图上 |
| `sub_B_roi` | ROI 减法（大图 − 小图） |
| `sub_A_roi` | ROI 减法（小图 − 大图） |
| `div_B_roi` | ROI 除法（大图 ÷ 小图） |
| `div_A_roi` | ROI 除法（小图 ÷ 大图） |

**特征检测与匹配**

| 算子 | 说明 |
|------|------|
| `cv_orb_detect` | ORB 特征点检测 |
| `cv_akaze_detect` | AKAZE 特征点检测 |
| `cv_bf_knn_match` | 暴力匹配 + KNN 筛选 |
| `cv_estimate_affine_partial2d` | 估计局部仿射变换矩阵 |

**EXIF 元数据**

| 算子 | 说明 |
|------|------|
| `write_image_exif` | 将字典中的元数据写入图像 EXIF 标签 |

### 工具算子

| 算子 | 说明 |
|------|------|
| `string_to_image` | 将字符串编码到 HALCON 图像中 |
| `image_to_string` | 从图像中解码字符串 |

## 项目结构

```
Halcon_SoftwarePackage/
├── def/                  # HALCON 算子定义文件
├── doc/                  # HTML 参考文档
├── examples/             # HDevelop 示例脚本
│   ├── sqlite.hdev
│   ├── modbus.hdev
│   ├── spdlog.hdev
│   ├── mysql.hdev
│   └── Image2String.hdev
├── include/              # 头文件
├── source/               # 源代码
├── CMakeLists.txt        # CMake 构建配置（纯 vcpkg，跨平台）
├── vcpkg.json            # vcpkg 依赖清单
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
* 创建滚动文件日志（5MB/文件，最多保留3个）
spdlog_rotating_logger_mt ('app', 'D:/logs/app.log', 5242880, 3, LogHandle)

* 设置级别为 debug，输出 debug 及以上的日志
spdlog_set_level (LogHandle, 1)

* 设置自定义格式
spdlog_set_pattern (LogHandle, '[%Y-%m-%d %H:%M:%S.%e] [%l] %v')

* 记录不同级别日志
spdlog_info (LogHandle, '程序启动完成')
spdlog_warn (LogHandle, '检测到配置缺失，使用默认值')
spdlog_err (LogHandle, '相机连接超时')

* 确保日志写入文件
spdlog_flush (LogHandle)

* 程序结束时关闭日志系统
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