---
name: halcon-extension-package
description: 'HALCON 扩展包算子开发与排错指南：覆盖新算子分流（有流程/多步算法先做进 cv_region 静态库再封装，该库已是单头 cvr.hpp + 单 cpp、非必要不新增文件；无流程的薄单算子直接写 supply 层）、DEF 语法规则、参数声明顺序、supply 层 C/C++ 约定、CH<op> 包装与三层命名、hcomp、CMake 整包构建、help 签名数据库同步、hrun/.hdev 例程测试。当创建、修改、调试、测试 HALCON 扩展包算子（.def 文件、supply 层、Halcon_SoftwarePackage、cv_region 静态库）时使用；当 HALCON 算子加载不上、被静默跳过、崩溃、返回空值或错误结果时使用；当 hrun/hdev 例程行为异常、hcomp 报错、扩展包构建失败（LNK1104）、DEF 解析报错时使用。'
---

# HALCON Extension Package — 项目专用技能

## 1. 架构与目录（cv_region 是**静态库**且已合并为**单头 + 单 cpp**）

```
cv_region/                          → 算法本体（C++17）：region 运算 + ransac 拟合
  ├─ add_library(cvr_core STATIC)   → 静态库 cvr_core，直接链入扩展包
  │    src/cvr.cpp                  → **全部实现**（含原内部头 model_expression.h /
  │                                     optimizer.h / ransac_core.h 与 OpenCV 桥、遗留 C ABI 段）
  │    include/cvr/cvr.hpp          → **全部声明**（唯一公开头，带 #pragma once）
  ├─ 依赖：STL + Eigen3::Eigen + muparser（PUBLIC 链出）；
  │    OpenCV 桥折在 cvr.hpp/cvr.cpp 末尾的 #ifdef CVR_WITH_OPENCV 段（子项目构建默认 OFF）
  └─ cv_region/tests/test_m1..m9     → 可独立构建跑单测（测试文件另算，不进单头单 cpp）
```

**⚠️ 文件数铁律（2026-09 起）：非必要不新增文件。**
- cv_region 里新增算子/函数：**写进既有的 `src/cvr.cpp` + `include/cvr/cvr.hpp` 对应段落**，不要新建 .hpp/.cpp（连 CMakeLists 都不用改）。
- 段落划分见两文件头部的映射表（原文件 → 段落），新代码放进语义最接近的段落即可。
- 只有「真正独立的新库/新模块」才建新文件（例如将来另起一个与 cv_region 无关的库）。
- 扩展包侧同理：仍是「每模块一个 DEF + 一个 supply cpp」，新增算子加进既有文件，不新建模块文件。

**把 cvr 给其它项目用（已实测，2026-09）**：拷 `include/cvr/cvr.hpp` + `src/cvr.cpp` **两个文件**即可（保持 `cvr/` 子目录，或改掉 cpp 里那行 `#include "cvr/cvr.hpp"`），但必须自备：
- 编译期 **Eigen3**（`<Eigen/Dense>` + `<unsupported/Eigen/LevenbergMarquardt>`）+ **muparser 头**（`<muParser.h>`）——缺了直接 `error C1083`（ransac 段在同一 TU 里且无开关，没法只编 region 部分）；
- 链接 **muparser** 库 + 运行期 **`muparser.dll`**（muparser 是 SHARED）；
- **不需要 OpenCV**（`<opencv2/core.hpp>` 在 `#ifdef CVR_WITH_OPENCV` 段内，只有用 region↔mask 互转才要）；
- 实测做法：外部工程 CMake 只 `find_package(Eigen3/muparser)` + `target_link_libraries(app PRIVATE Eigen3::Eigen muparser::muparser)` + 把 `cvr/cvr.cpp` 加进源列表，则 **C++ API / ransac / 遗留 C ABI 三层都可用可跑**（示例输出：`outer_radius=40.1295`、`ransac a=3.0 b=2.0`、`C ABI area=200`）。
- 两个坑：① **调用方自己的含中文源文件必须 UTF-8 with BOM 或加 `/utf-8`**，否则 MSVC 按 936 解码会「注释吞掉下一行」并报一堆莫名错误（伴 `warning C4819`）；两个拷走的文件本身带 BOM 没问题。② `cvr_*` C ABI 是 **1 = 成功 / 0 = 失败**，而 `ransac_*` 是 **0 = 成功 / 负错误码**，两套约定不同（已写进 cvr.hpp 注释）。

Halcon_SoftwarePackage/             → 扩展包（沿用现有包，勿新建）
  ├── def/Halcon_<Module>.def       → 算子注册（每模块一个文件）
  ├── source/Halcon_<Module>.cpp    → supply 层（参数读取 + 调核心/第三方库 + 输出）
  ├── source/Halcon_SoftwarePackage.c → CH<op> 包装函数（H<op> → supply）
  └── include/Halcon_SoftwarePackage.h → H<op> 声明
```

**模块 ↔ 实现位置（现状，新增算子时照此对齐；分流判据见 §7）**：

| 模块 | 算子数 | 实现位置 |
|---|---|---|
| `Halcon_CVRegion.def` | 37 | 算法在 `cvr_core`（`cvr::`，58 处调用），supply 只做参数搬运 |
| `Halcon_Ransac.def` | 1 | 算法在 `cvr_core`（`ransac::ransac_run`） |
| `Halcon_OpenCV.def` | 43 | 直接写在 supply（`cv::`）；唯一例外 `cv_measure_*` 复用 `cvr::cvr_measure_pos` |
| `Halcon_Math` / `Sqlite` / `Modbus` / `Spdlog` / `Mysql` | 11/5/14/20/3 | 直接写在 supply（muparser+Eigen+armadillo / sqlite3 / libmodbus / spdlog / libmysql） |

**「cv_region 合并成单头 + 单 cpp」——已于 2026-09-27 执行完成（实测数据存档，勿重复测量）**：

- **结果**：`include/cvr/cvr.hpp`（945 行，唯一公开头）+ `src/cvr.cpp`（4463 行，唯一实现），旧 12 头 + 15 源全部删除；`add_library(cvr_core STATIC src/cvr.cpp)`；扩展包/测试的 include 统一为 `#include "cvr/cvr.hpp"`。合并方式：脚本机械拼接（去 `#pragma once`/内部 include、顶层标准头提到文件头、条件块内 include 原地保留、每段加「原 xxx 文件」横幅）。
- **校验做法（改这个库时必须复用）**：① 新旧文件「全部 `cvr_*` 标识符集合」做 `comm` 比对，必须**零丢失**；② `#if/#endif` 配对数必须与旧集合相等（本次 10/10）；③ 全量例程回归 + 独立单测 + DLL 体积对比，三者与合并前一致才算过。
- **实测代价**：① 增量编译 **1s → 整 TU 重编**（单 TU 编译约 14s，整包构建 17s）；② 单 TU 的 `cvr.obj` **10,352 KB**（旧 11 个 obj 合计仅 2010 KB），`cvr_core.lib` **2.3 MB → 15.2 MB**（单 obj + 库索引），但**最终 DLL 1,085,952 字节（比合并前还小 1.5 KB）**——链接期 `/OPT:REF` 仍会剔除未引用函数，`cvr_c_api` 死代码探针依旧为 0；③ 曾试加 `/Gy`：obj/lib/DLL **完全无变化**，故不需要（结论已写进 `cv_region/CMakeLists.txt`）；④ Eigen/muParser 现在进入 region 部分的每个编译单元；⑤ OpenCV 桥折成 `#ifdef CVR_WITH_OPENCV` 段（`cvr.hpp` 里 `<opencv2/core.hpp>` 在该段内，不定义宏时头文件不依赖 OpenCV——**这条必须守住**，否则所有 consumer 被强制要求 OpenCV 头）。
- **踩过的坑（改单文件库时会再遇到）**：
  1. `ransac::ExprInvalid` / `ExprBudgetExceeded` 原本是经 `ransac_core.h → model_expression.h` **传递**给 supply 的（供 `catch` 按类型映射错误码），折叠成单头时若只搬 `CoreOptions/CoreResult/ransac_run` 就会编译失败 → 消费端要用的异常类型**必须放公开头**（它们不依赖 muparser，无害）。
  2. 内部头（`optimizer.h` 等）里的 `#include "cvr/..."` 必须删掉，否则指向已不存在的小头。
  3. 判定「条件编译里的 include 是否安全」要按 `#if` 嵌套深度处理，不能把所有 `#include <...>` 都提到文件头（`<opencv2/core.hpp>` 一旦被提出 `#ifdef` 就破坏可选依赖）。

**三层命名**（缺一不可，名字必须对应）：DEF 物理名 `CHcv_union2`（`cv_union2<- CHcv_union2[...]`）→ 包装函数 `CHcv_union2`（.c 中 `return Hcv_union2(ph);`）→ 实现函数 `Hcv_union2`（模块 cpp；OpenCV 模块实现函数带 C 前缀 `HCcv_xxx`，对应 `Ccv_xxx`）。

**参数全开放约定**：算子可调参数尽量全开放（默认值与底层库一致）。图像/滤波/矩阵类用控制参数按位传满；特征检测/匹配/变换估计类（orb/sift/akaze/bf_knn/affine_partial/rigid）用 dict 键传入，未设键取默认。

## 2. DEF 文件规则（出错率最高的地方）
   > **value_list 不要写双引号**：`value_list: a,b,c;`（带引号虽能过 hcomp，但 HDevelop 的建议值提示会带上引号，也与 HALCON 官方 .ref 写法不一致）。

**编码**：UTF-8 **无 BOM**、首行不能是空行（否则 hcomp 报 "operator declaration expected"）。
**签名格式**：`算子名<- CH物理名[输入对象:输出对象:输入控制:输出控制]`；全控制参数（无对象）用 `[::In1,In2:Out1,Out2]`（两个冒号开头）。
**参数声明块顺序必须严格 = 签名顺序**：全部输入对象 → 全部输出对象 → 全部输入控制 → 全部输出控制。新参数追加在段末尾。
**控制参数必填**：`default_type`（handle/integer/real/string）、`sem_type`、`multivalue`；tuple 用 `multivalue: true`。
**枚举/宏参数**：DEF 用 `type_list: string,integer`，supply 用 `read_enum_param`（先按字符串取，表映射失败再按数值字符串/整数解析）。映射用小表线性查找（≤16 项，比哈希表快），大小写不敏感；Threshold 类可 "|" 组合（"binary|otsu"）。
**tuple 类型**：坐标/数值 tuple 参数若声明 `type_list: real`，传整数 tuple 会报 1203「Wrong type of control parameter」（HALCON 在校验层就拒）。要兼容整数字面量：DEF 用 `type_list: real,integer`，supply 用 `HGetPElemD(ph, par, CONV_CAST, ...)`（CONV_NONE 不转整型）。
**默认值**：`default_value` 仅供 C/C++/Python 接口，HDevelop 调用必须传满全部参数（见 §6）。
**帮助文档**：①每个参数块可加 `description.english: 描述;` 槽（hcomp 生成 HTML 参数说明；**每块至多一条**，重复会让 hcomp 静默跳过整页生成；文本内 ASCII `;` 转全角）；②`short.english` 渲染在 F1 帮助页 Name 节——写 2~4 句详细用法（功能+对应库函数/输入约束/关键参数/注意事项），这是短头格式下唯一可用的"长说明"渠道；③算子级 `description.english`/`example.trias`/`attention`/`see_also` 槽在短头格式下 hcomp 不接受或不在 HTML 渲染，别用。

## 3. Supply 层约定（`source/Halcon_<Module>.cpp`）

**关键规则**：
1. `HGetXxx`/`HNewRegion`/`HCopyObj` 等宏是语句不是表达式（内部带 `return Herror`），只能用在返回 `Herror` 的函数里；返回指针/bool 的 helper 一律直接调 `HP*` 函数（`HPGetObj`、`HPGetComp`、`HPGetFRL`、`HPGetCPar`、`HPGetCParNum`）。
2. 读 region：`HGetComp(ph, obj_key, REGION, &region_key)` + `HPGetFRL(ph, region_key, &hrl)`（零拷贝指针）。
3. 写 region：`HAllocRLNumTmp(ph, &hrl, n)` + 填 `is_compl/num/num_max/rl[]` + `HPNewRegion(ph, hrl)` + `HFreeRLTmp(ph, hrl)`。
4. 读数值 tuple：`HGetPElemD(ph, par, CONV_CAST, &ptr, &n)`（CONV_CAST=1 接受整数字面量）。
5. 读字符串 tuple：`HGetPElemS(ph, par, CONV_NONE, &arr, &n)`；**读字符串参数前必须先 `HAllocStringMem(ph, 1024)`**（且每算子只调一次，多次调用会泄漏 temp memory）。
6. 读全局变量：`HAccessGlVar(ph, HGWidth, GV_READ_INFO, &ival, 0.0, nullptr, 0, 0)`——整型全局必须用 `INT4_8` 缓冲接。
7. 写输出 tuple：`HPutElem(ph, par, ptr, n, DOUBLE_PAR/LONG_PAR)`；整数用 `INT4_8`。
8. 写字符串输出：`HPutElem(ph, par, &msgOut, 1, STRING_PAR)`——必须 `char**`（`char* msgOut = buf; &msgOut`）。
9. 自定义错误码 > 10000 且返回前 `HSetErrText`。
10. 无输入对象的生成类算子（gen_*）不调 `HCkNoObj`。

## 4. 高频踩坑清单

| 坑 | 现象 | 解决 |
|---|---|---|
| 源文件含中文注释无 BOM | 大量诡异语法错误（C2059/C2614） | 所有 .h/.cpp 保存为 UTF-8 BOM；DEF/hdev 无 BOM |
| 读字符串参数前没 `HAllocStringMem` | 0xC0000005 闪退 | 读字符串前 `HAllocStringMem(ph, 1024)` |
| `HPutElem STRING_PAR` 传 `char*` | 0xC0000005 | 传 `char**`（`&msgOut`） |
| 整数全局用 `double*` 接 | 读到 denormal 垃圾 | 用 `INT4_8` 缓冲 |
| HDevelop 少传参数 | 该行**静默跳过**，输出为空且断言**假性通过** | 传满全部参数；`-d` dump 确认变量真被赋值 |
| 变量类型冲突（同名既 iconic 又 control） | 相关行整段判为无效 | iconic/control 变量名分开 |
| 对象 tuple 字面量 `[R1,R2]` | hrun 判为无效 | 对象 tuple 用 `concat_obj` |
| DEF 用了 BOM / 空行开头 | "operator declaration expected" | 无 BOM、去首空行 |
| 包目录名 ≠ 包名 | 算子加载不到 | 目录名必须等于包名 |
| `HALCONEXTENSIONS` 用正斜杠 | 包加载不到 | Windows 下必须用反斜杠路径 |
| 重建时 DLL 被进程占用 | LNK1104 | 先杀掉残留 hrun/hdevelop 进程 |
| 形态学 SE 参考点当坐标原点 | closing 破坏扩展性 | HALCON 参考点 = SE 质心四舍五入；膨胀用居中 SE 的反射；矩形 SE 从 0 生成再居中 |
| `pow(x,2)` 表达式解析失败 | muparser "Unexpected token pow" | `DefineFun("pow", &my_pow)` 注册 |
| supply 泄漏 temp memory | hrun 弹窗「after procedure X: number of still allocated temp memory blocks: N」，X 会变；阻塞时批量脚本莫名超时 | 两个来源：①`HAllocTmp` 不 `HFreeTmp(proc,ptr,size)`（size=分配同值，**错误返回路径也要释放**）；②**同一算子里多次 `HAllocStringMem`**（proc 只自动释放一块）。每算子开头统一分配一次 |
| PS5.1 `Set-Content -Encoding UTF8` 写 DEF | 文件头被加 BOM → hcomp 报 "operator declaration expected" | DEF/无 BOM 文件一律用 `[System.IO.File]::WriteAllLines(path, lines, (New-Object System.Text.UTF8Encoding($false)))` |
| bash 管道里写中文 `-match`/` -ceq` 字面量 | 匹配恒失败（命令文本编码在管道中被损坏） | 中文脚本写成 **.ps1 文件（Write 工具写 UTF-8 再加 BOM）+ `powershell -ExecutionPolicy Bypass -File` 执行**；脚本内中文匹配可靠 |
| PowerShell 函数命名 `FL`/`gp`/`sl` 等 | 函数被内置别名遮蔽（如 `fl`=Format-List），调用返回空 | 函数名避开别名：FindX/GetX 等；赋值前 `Get-Alias <name>` 查一下 |
| `cv::kmeans` 传 flags=1 (PP_CENTERS) | OpenCV 4.12 内部断言 | flags=0 (RANDOM_CENTERS) 稳定；PP 在该版本有此问题 |
| `BFMatcher(crossCheck=true)` 仍调 `knnMatch` | 匹配数恒为 0 | crossCheck 必须走 `bf.match()` |
| 对 object 用 `\|Conn\|` 求长度（`\|...\|` 只对 tuple 有效） | **该变量涉及的所有行**（含赋值它的那行）一并判为无效行、静默 no-op；`hrun -v` 只报「Control variable is not instantiated」，看不出真凶 | 数对象用 `count_obj(Conn, N)`；用 `hrun -P` 列出全部无效行 |
| HDevelop 实参顺序写错（把**输出**控制参数放到**输入**控制参数前） | 该行判无效、静默跳过、输出为空 → 断言假性通过 | 线性顺序 = `输入对象, 输出对象, 输入控制, 输出控制`；全控制参数算子 = `(输入对象, 输入控制, 输出控制)`，如 `cv_region_features(Regions, Features, Values)`（`Values` 在最后） |
| 无效行「传染」 | 一个变量被非法使用后，所有赋值/使用它的行都判无效，故障面远大于一行 | 先 `hrun -P` 拿到完整无效行清单，再回头改源头那一行 |
| 核心库单测用 Release（MSVC `/DNDEBUG`）编译 | `assert` 全部被编掉 → 测试只打印不校验，`M5 ALL PASS` 之类是**假通过** | 别只信单测结论；关键数值改用 `hrun` 与 HALCON 原生算子逐值对照 |
| supply 里 `HGetPElemD` 的 ELEM 形参类型写错 | C2664「无法将 `double **` 转换为 `const double *__restrict *`」 | 指针声明为 `double const*`（照抄 `Halcon_Ransac.cpp` 的 `double const* init_vals = nullptr;`） |

## 5. 构建

- **cv_region 静态库随整包一起编**（顶层 `add_subdirectory(cv_region)` → `cvr_core`，包 `target_link_libraries(... cvr_core ...)`）：改核心库只需整包重建，不用单独构建。核心库现在是**单 TU**（`src/cvr.cpp`），改任何一行都会重编整个 TU（约 14s，整包 17s）——这是单文件的既定代价。
- **CMake 版本必须与既有 build 树一致**：`cv_region/build` 是用 `C:/cmake-3.30.5-windows-x86_64` 配置的，用 PATH 上的 cmake 4.4.3 去 `--build` 会触发重新配置并报 `No preprocessor test for "Renesas"`（3.30 模块被 4.4 读取），**看起来像构建失败、实际是版本混用**（`ctest` 随后会拿旧二进制"通过"= 假通过）。一律用固定路径：`C:/cmake-3.30.5-windows-x86_64/bin/cmake.exe`（与 `cv_region/.vscode/settings.json` 的 `cmake.cmakePath` 一致）。
- 要跑核心库单测才需独立配置一次。**必须带 vcpkg 工具链**：`cv_region/` 下没有 vcpkg.json，裸跑 `cmake -B cv_region/build -S cv_region` 会在 `find_package(muparser/Eigen3)` 处直接失败（实测 exit=1）；要复用父工程 `build/vcpkg_installed` 那棵树：
  ```powershell
  C:/cmake-3.30.5-windows-x86_64/bin/cmake.exe -B cv_region/build -S cv_region -G "Visual Studio 16 2019" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_MANIFEST_MODE=OFF `
    -DVCPKG_INSTALLED_DIR=<包根>/build/vcpkg_installed `
    -DCMAKE_PREFIX_PATH=<包根>/build/vcpkg_installed/x64-windows `
    -DCVR_BUILD_TESTS=ON -DCVR_BUILD_OPENCV=ON
  cmake --build cv_region/build --config Release
  ctest --test-dir cv_region/build -C Release      # 9 个测试 test_m1..m9
  ```
  （本机这套参数已写在 `cv_region/.vscode/settings.json`，用 VS Code 打开该目录可直接配置/调试。）
- hcomp 分两次调用：`-H` 生成包初始化 `H<name>.c`（-p<包名>，多个 DEF 一次调用）；`-C` 生成 C 接口 `HC<name>.c/.h`。
- 本项目用 MVTec 官方 `UseHALCON.cmake` 的 `HALCON_AddExtensionPackage(name DEF_FILES ... SOURCES ...)`，自动生成 hcomp 调用 + 主库/接口/help/html 目标。
- 改 DEF 后必须 `cmake --build build --config Release`（不带 --target）整包重建——接口变体 c/cpp/dotnet 是独立目标；POST_BUILD 会同步 help 数据库到包 help/。
- **新增算子别追加到 DEF 末尾**：hcomp 按 DEF 文件**逆序**编号（index 0 = 文件里最后一个算子），追加到末尾会让既有算子注册索引整体 +1、`help/operators_en_US.*` 全量位移；把新算子块插到**文件开头**可让既有索引与 help 库行位置保持不变（只多一行）。改完用 `hcomp -u -H -pcheck <def>` 生成 `Hcheck.c`，抽查 `HOIID_LogicalName` 的 index，确认老算子没被动。
- 产物命名固定：`<包名>.dll`、`<包名>c.dll`、`<包名>cpp.dll`、`<包名>dotnet.dll`，不得改名。
- 激活：`HALCONEXTENSIONS` = 包目录完整路径（反斜杠）；改注册表后重开 HDevelop/终端生效。
- HTML 帮助目标（`..._html`）增量构建不可靠（MSB8065，改了 DEF 也可能不重生成）：手动全量——在 `build/doc/html/reference` 下跑 `hcomp -u -R -p<包名> -len_US <全部 def>`，再 `Copy-Item build/doc/html/* doc/html/ -Recurse -Force`（同理 help db：`Copy-Item build/help/operators_en_US.* help/ -Force`）。

## 6. 测试（hrun）

```powershell
hrun -v script.hdev    # 跑，失败打印错误
hrun -d script.hdev    # 跑完 dump 所有变量值（验证输出真被赋值）
hrun -P script.hdev    # pedantic：列出「无效程序行」行号——定位静默跳过的唯一快速手段
```

- .hdev 是 XML（`<hdevelop><procedure name="main"><body><l>...</l></body></procedure></hdevelop>`）；文本里 `<` 转义 `&lt;`、`>` 写 `&gt;`、`&` 写 `&amp;`。
- 本仓库例程统一 **UTF-8 无 BOM + CRLF**（实测 `hrun` 按 XML 声明 `encoding="UTF-8"` 正常解析）。别照搬"含中文必须加 BOM"的说法。
- 断言用 `throw('msg')`；例程退出码 0 才算过。

> **例程图像默认用 HALCON 自带例图 `printer_chip/printer_chip_01`**：`read_image (Image, 'printer_chip/printer_chip_01')`。
> 不要自己造图、也不要引用仓库里不存在的图片文件；需要别的像素类型时用 `convert_image_type (Image, Out, 'uint2'|'int4'|'real'|...)`（多通道用 `rgb1_to_gray`/`channels_to_image`）转换即可。
> 若例程确实还需要**别的资源文件**（其它图像、标定文件、模型、数据库、点云…）——**先问用户**，别擅自引入或假设其存在。

- **无效行 = 静默 no-op**：调用写错时该行不执行也不报错，输出为空，`if` 断言**假性通过**。定位三步：
  1. `hrun -P` → 直接列出无效行行号（`-v` 只报「变量未实例化」时，必须靠这步找真凶）；
  2. `hrun -v` → 看运行到哪一行炸；
  3. `hrun -d` → 确认变量有真值，不要只看退出码。
- **HDevelop 实参顺序**（写错即无效行）：线性顺序 = `输入对象, 输出对象, 输入控制, 输出控制`。全控制参数算子写作 `(输入对象, 输入控制, 输出控制)`——如 `cv_region_features(Regions, Features, Values)`（输出 `Values` 在最后）；含输出对象的则如 `cv_bin_to_region(BinImage, Region, Threshold)`。
- **别对 object 用 `|...|`**：`|Tuple|` 只适用于 control tuple。`|Conn|` 会让所有涉及 `Conn` 的行（包括 `connection`、`count_obj`）一起判为无效并静默跳过。数对象必须用 `count_obj`。
- 用 WMI `Win32_Process Create` 起全新进程验证环境变量（别用当前 shell 子进程）。
- **推荐验收口径**：与 HALCON 原生算子对照——region 用「对称差面积 = 0」判精确一致；特征值逐值比 1e-9。注意先确认核心库实现是否真与原生一致：批量算子（`cv_region_features`）与专用算子（`cv_contlength` / `cv_circularity` / …）都走 `cvr_get_feature`，两者**应逐位一致**，可互为回归基准（只要专用算子与原生一致，批量算子就一致；反之偏差是核心库既有问题，不是封装引入的）。

## 7. 新算子速查流程（**第一步先分流**：算法进静态库，还是直接写 supply）

**分流判据**（决定算法写在哪，先判断再动手）：

| 判据 | 去哪 | 例子 |
|---|---|---|
| **有流程**：多步算法 / 几何-数值核心、需与 HALCON 口径逐值对齐、会被多个算子复用、需要独立单测 | **先做进 `cv_region` 静态库**（加进 `cvr_core`），再封装 | `cv_shape_trans`（最小外接矩形→角点→栅格化）、`cv_region_features`（特征公式表）、`cv_select_shape`、`cv_gen_rectangle2`、`cv_measure_*`（1D 边缘流水线）、`cv_ransac_fit`（迭代拟合 + LM） |
| **无流程单算子**：薄映射，一次库调用 | **直接写进扩展包 supply**，不动 `cv_region` | `cv_add`/`cv_subtract`/`cv_multiply`/`cv_divide`（各一次 `cv::`）、`cv_spdlog_*`、`cv_sqlite_*`、`cv_modbus_*`、`cv_mysql_*`、`cv_math_*` |

> ⚠️ 进 `cvr_core` 的代码只能用 **STL + Eigen + muparser**（库的既有依赖，PUBLIC 链出）。依赖 OpenCV 的流程写成 `cvr.hpp`/`cvr.cpp` 末尾 `#ifdef CVR_WITH_OPENCV` 段里的函数（该宏在扩展包构建下默认不开），或留在 supply。

**A. 有流程算子 → 做进 cv_region 静态库（先做这步，再封装）**
1. `cv_region/include/cvr/cvr.hpp` 的**对应段落**加声明（注释写清对应的 HALCON 算子 + 已知口径差异）。
2. `cv_region/src/cvr.cpp` 的**同语义段落**实现；需要新单测就加 `cv_region/tests/test_mN_*.cpp`（测试文件另算）。
3. **不改 CMakeLists、不新建文件**（单头单 cpp 的既定收益；段落映射见两文件头部表）。只有当新函数依赖新的第三方库时才需要动 CMake。
4. 独立构建 + 单测（**必须用 pinned cmake**，见 §5）：`C:/cmake-3.30.5-windows-x86_64/bin/cmake.exe --build cv_region/build --config Release` → 同路径 `ctest.exe --test-dir cv_region/build -C Release`。
   ⚠️ 单测是 Release/MSVC（`/DNDEBUG`）编译，`assert` 全被编掉 → 单测"全过"不可信，关键数值必须再用 `hrun` 与 HALCON 原生算子逐值对照（§6）。
5. 然后走下面 B 的封装步骤（否则 HDevelop 里看不到该算子）。

**B. 封装进扩展包（两类算子都一样）**
1. `def/Halcon_<Module>.def` 加算子块（遵循 §2），**插到文件开头**（索引规则见 §5）；单独 `hcomp -u -H -pcheck <def>` 验语法（exit=0）。
2. `source/Halcon_<Module>.cpp` 写 `Hcv_<op>`（遵循 §3）；有流程的在这里 `#include "cvr/xxx.hpp"` 直接调 C++ API。
3. `source/Halcon_SoftwarePackage.c` 加 `CHcv_<op>` 包装；`include/Halcon_SoftwarePackage.h` 加声明。
4. 整包构建（`cmake --build build --config Release`，不带 `--target`）→ 写 `.hdev` 例程到 `examples/` → `hrun -v` + `-P` + `-d` 验证。
5. README 补算子文档（原型/参数/错误码/例程）。

## 8. 【血泪教训】改了算子签名后"不被调用"（静默跳过、输出空、无报错）

**根因**：HALCON 做调用校验时从包目录 `help/operators_en_US.num`（算子签名数据库）读参数个数，**不是**只从 DLL 注册读。该库每行 4 个数 = `入对象 出对象 入控制 出控制`，按注册顺序排列。

**坑**：CMake 构建只把它生成到 `build/help/`，不拷贝到包 `help/`。于是改了签名的旧算子按旧签名校验失败 → 该行被静默跳过；新算子（旧库没有）和没改的算子正常——这就是"新算子好使、改签名的旧算子不好使"的原因。

**诊断**（某算子"明明注册了却不被调用"时）：
1. supply 第一行加 `return 30099;`（>10000 自定义错误码），重编后调用：报 30099 = 被派发；不报错且输出空 = 没派发（卡在校验）。
2. 用旧签名（旧参数个数）调用一次：能派发 → 确认 HALCON 用的是旧签名 → help 库没同步。
3. 对照一个全新名字的同签名算子：新名好使 → 按名字取了旧签名。

**解决（已固化进 CMakeLists）**：主库 POST_BUILD 加 `copy_directory ${CMAKE_BINARY_DIR}/help → ${CMAKE_CURRENT_SOURCE_DIR}/help`。每次改 DEF 后整包重建。手动补救：`Copy-Item build\help\operators_en_US.* help\ -Force`。

## 9. 文档维护工作流（README ↔ DEF ↔ HTML 帮助）

1. **写算子文档的顺序**：README 章节（#### 节：一句话/原型/参数表/错误码）→ 例程 hdev → DEF 参数 `description.english`（可从 README 表自动注入：解析 `| 参数 | 类型 | 方向 | 说明 |` 表 → 按算子节名+参数名匹配插入参数块）→ `short.english` 详细化（F1 Name 节）→ `hcomp -R` 重生成 HTML 并拷贝到 `doc/html/`。

2. **README 编码是 UTF-8（无 BOM）**——改动前先验字节（`UTF8 GetBytes("说明")` 在文件中匹配计数），别凭控制台显示猜；含中文的批量编辑走 .ps1 文件 + BOM + Bypass。

3. **验收口径**：`hcomp -u -R` 输出 0 个 "Missing description" / 0 error；`hrun` 全量例程通过；F1 页 Name 节有详细说明、参数表每条有中文含义。


## 参考文件（本项目内）

- 核心库（**只有两个文件**）：`cv_region/include/cvr/cvr.hpp`（全部声明）+ `cv_region/src/cvr.cpp`（全部实现）；两文件头部都有「原文件 → 段落」映射表，加函数时按段落找位置。
- 现有可抄的 supply：`source/Halcon_CVRegion.cpp`（region 类）、`source/Halcon_Ransac.cpp`（全控制参数+字符串+数值 tuple）、`source/Halcon_Math.cpp`（muparser + Eigen LM、字符串输出 msgOut 模式）。
- 可抄的 DEF：`def/Halcon_CVRegion.def`（对象+控制+value_number）、`def/Halcon_Ransac.def`（全控制参数）。
- 手册：`extension_package_programmers_manual.pdf`（本仓库根目录；HAllocStringMem §5.5.10、HGetRL/HAllocRLTmp R7、包结构 §1.2.3、错误码 R6）。
