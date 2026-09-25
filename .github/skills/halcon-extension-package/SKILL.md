---
name: halcon-extension-package
description: 'Use when creating, modifying, debugging, or testing HALCON extension package operators (.def files, supply layer, hcomp, Halcon_SoftwarePackage, cv_region/ransac standalone libs), or when a HALCON operator fails to load, is silently skipped, crashes, or returns empty/wrong results, or when hrun/hdev examples misbehave. Covers DEF syntax rules, supply C/C++ conventions, CMake build, help database sync, and hrun testing.'
---

# HALCON 扩展包开发

在 `Halcon_SoftwarePackage` 中新增/修改算子时遵循本流程。核心架构：**算法本体做成独立 DLL（C ABI 外接口，可脱离 HALCON 单独调用），扩展包只做薄 supply 封装层**。

## 1. 架构与目的

```
独立库（cv_region/ 或 ransac/）       → 算法本体，C ABI（句柄式，无 C++ 类型的 DLL）
  ├── include/<lib>/xxx_c_api.h      → extern "C" 头文件
  └── src/*.cpp                      → 编译出 <lib>.dll，输出到 bin/

Halcon_SoftwarePackage/              → 扩展包（沿用现有包，勿新建）
  ├── def/Halcon_<Module>.def        → 算子注册（每模块一个文件）
  ├── source/Halcon_<Module>.cpp     → supply 层（参数读取 + 调 DLL + 输出）
  ├── source/Halcon_SoftwarePackage.c → CH<op> 包装函数（H<op> → supply）
  └── include/Halcon_SoftwarePackage.h → H<op> 声明
```

**三层命名法**（缺一不可，名字必须对应）：

| 层 | 名字 | 位置 |
|---|---|---|
| DEF 物理名 | `CHcv_union2` | `def/*.def` 里 `cv_union2<- CHcv_union2[...]` |
| 包装函数 | `CHcv_union2` | `source/Halcon_SoftwarePackage.c`，`return Hcv_union2(ph);` |
| 实现函数 | `Hcv_union2` | `source/Halcon_CVRegion.cpp`，头文件里 `extern EXPORTS_API Herror Hcv_union2(...)` |

**参数全开放约定**：算子可调参数应尽量全开放（默认值与底层库一致）。**图像/滤波/矩阵类**用控制参数按位传满；**特征检测/匹配/变换估计类**（orb/sift/akaze/bf_knn/affine_partial/rigid）用 dict 键传入（`set_dict_tuple` 写入后传 DictHandle，未设键取默认，输入图像/描述子/点集也以 dict 对象传，结果写回同一 dict）。

## 2. DEF 文件规则（出错率最高的地方）

**编码**：UTF-8 **无 BOM**、首行**不能是空行**（否则 hcomp 报 "operator declaration expected"）。

**签名格式**：`算子名<- CH物理名[输入对象:输出对象:输入控制:输出控制]`

- 对象在前、控制在后；段间用 `:` 分隔；无对象的段留空。
- 全控制参数（无对象）：`[::In1,In2:Out1,Out2]`（两个冒号开头，不是三个）。
- **参数声明块的顺序必须严格 = 签名顺序**：全部输入对象 → 全部输出对象 → 全部输入控制 → 全部输出控制。新参数**追加在段末尾**保持位置兼容；新输入参数插到第一个输出参数之前，不能加到文件末尾。

**参数名**：禁止下划线（用 `ModelExpression`、`ParamNames` 这种驼峰）；算子名全小写下划线。

**控制参数必填项**：`default_type`（handle/integer/real/string）、`sem_type`、`multivalue`（false/true/optional）；tuple 用 `multivalue: true`。

**value_number**：关键字**带冒号** `value_number:`，布尔运算用 `&&`/`||`（不是 `and/or`、不是单个 `&`/`|`）。例：`RegionUnion == Region1 && (Region2 == Region1 || Region2 == 1);`

**默认值 `default_value` 仅供 C/C++/Python 接口**，HDevelop 调用**必须传满全部参数**（见 §6）。

**tuple 类型**：坐标/数值 tuple 参数若声明 `type_list: real`，传**整数 tuple 会报 1203「Wrong type of control parameter」**（HALCON 在校验层就拒，根本不进 supply）。要兼容整数字面量：DEF 用 `type_list: real,integer`，且 supply 用 `HGetPElemD(ph, par, CONV_CAST, ...)` 读（`CONV_NONE` 不把整型转 double）。

## 3. Supply 层约定（`source/Halcon_<Module>.cpp`）

```cpp
#include "HalconCpp.h"
#include "HDevThread.h"
#include "Halcon_SoftwarePackage.h"
#include "<lib>/xxx_c_api.h"

Herror Hcv_xxx(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);               // 有输入对象时第一行
    HAllocStringMem(proc_handle, 1024);  // 凡是要读字符串参数，必须先分配（手册 5.5.10）
    ...
}
```

**关键规则**：

1. **`HGetXxx`/`HNewRegion`/`HCopyObj` 等宏是语句不是表达式**（内部带 `return Herror`），只能用在返回 `Herror` 的函数里；返回指针/void 的 helper 一律直接调 `HP*` 函数（`HPGetObj`、`HPGetComp`、`HPGetFRL`、`HPGetCPar`、`HPGetCParNum`）。
2. **读 region**：`HGetComp(ph, obj_key, REGION, &region_key)` → `HPGetFRL(ph, region_key, &hrl)`（零拷贝指针，不分配，避开 R7 重试循环）。`Hrun{l,cb,ce}` 逐字段转 `int32`（普通版 int16 / XL int32）。
3. **写 region**：`HAllocRLNumTmp(ph, &hrl, n)` → 填 `is_compl/num/num_max/rl[]` → `HPNewRegion(ph, hrl)` → `HFreeRLTmp(ph, hrl)`（注意传 `hrl` 不是 `&hrl`）。写出 `select_shape` 类子集用 `HCopyObj(ph, in_key, 1, &out_key)`（引用语义）。
4. **读数值 tuple**：`HGetPElemD(ph, par, CONV_CAST, &ptr, &n)`（`CONV_CAST=1` 接受整数字面量；`CONV_NONE=0` 不转换）。
5. **读字符串 tuple**：`HGetPElemS(ph, par, CONV_NONE, &arr, &n)`（指针指向 HALCON 内部，调用期间有效）。
6. **读全局变量**：`HAccessGlVar(ph, HGWidth, GV_READ_INFO, &ival, 0.0, nullptr, 0, 0)` ——**整型全局必须用整型缓冲接**（用 `INT4_8`，不是 `double*`），`H_MSG_OK=2`。
7. **写输出 tuple**：`HPutElem(ph, par, ptr, n, DOUBLE_PAR/LONG_PAR)`；整数用 `INT4_8`（现有包惯例，LONG_PAR 是 8 字节）。
8. **写字符串输出**：`HPutElem(ph, par, &msgOut, 1, STRING_PAR)` ——**必须用 `char**`**（`char* msgOut = buf; &msgOut`），用 `char*` 会解引用崩溃。
9. **自定义错误码 > 10000** 且返回前 `HSetErrText(const_cast<char*>(...))`（HSetErrText 参数是 `char*`）。
10. 无输入对象的生成类算子（gen_*）**不调** `HCkNoObj`。

## 4. 高频踩坑清单

| 坑 | 现象 | 解决 |
|---|---|---|
| 源文件含中文注释无 BOM | 大量诡异语法错误（C2059/C2614）、`std` 嵌进项目命名空间 | 所有 .h/.cpp/.hpp 保存为 **UTF-8 带 BOM**；DEF/hdev 无 BOM |
| 读字符串参数前没 `HAllocStringMem` | 0xC0000005 访问违例闪退 | 读字符串前 `HAllocStringMem(ph, 1024)` |
| `HPutElem STRING_PAR` 传 `char*` | 0xC0000005 | 传 `char**`（`&msgOut`）|
| 整数全局用 `double*` 读 | 读到 denormal 垃圾（位模式被当 double）| 用 `INT4_8` 缓冲 |
| HDevelop 少传参数 | 该行**被静默跳过**，输出为空且空输出断言**假性通过** | **传满全部参数**；检查输出变量是否真的被赋值（用 `-d` dump 确认）|
| 变量类型冲突（同名既当 iconic 又当 control）| 相关行整段判为无效 | iconic 与 control 变量名分开（如 gen 目标用 `Rect1` 不用 `R1`）|
| tuple 字面量 `[R1,R2]` | hrun 判为无效行 | 对象 tuple 用 `concat_obj` |
| DEF 用了 BOM / 空行开头 | "operator declaration expected" | 去 BOM、去首空行 |
| 包目录名 ≠ 包名 | 算子加载不到 | 目录名必须等于包名（`cvr/` 之于 `-pcvr`）|
| `HALCONEXTENSIONS` 用正斜杠 | 包加载不到 | Windows 下必须用**反斜杠**路径 |
| 重建时 DLL 被进程占用 | LNK1104 无法打开文件 | 先杀掉残留 hrun/hdevelop 进程 |
| 形态学 SE 参考点当坐标原点 | closing 破坏扩展性（结果偏小）| HALCON 参考点 = **SE 质心四舍五入**；膨胀用居中 SE 的反射；`gen_*` 裁剪负坐标；矩形 SE 从 0 生成再居中 |
| `pow(x,2)` 表达式解析失败 | muparser "Unexpected token pow" | `DefineFun("pow", &my_pow)` 注册 |
| `cv::kmeans` 传 flags=1 (PP_CENTERS) | OpenCV 4.12 内部断言 `_labels.at(i) < K`（30003） | flags=0 (RANDOM_CENTERS) 稳定；PP 在该 4.12 构建有此问题（DLL 与源码行号都对不上），要试就 attempts=1 |
| `BFMatcher(crossCheck=true)` 仍调 `knnMatch` | 匹配数恒为 0（crossCheck 在 knnMatch 语义下不生效） | crossCheck 必须走 `bf.match()`；只有 crossCheck=false 才用 `knnMatch`+ratio test |

## 5. 构建

- **hcomp 分两次调用**：`-H` 生成包初始化 `H<name>.c`（`-p<包名>`，多个 DEF 一次调用）；`-C` 生成 C 接口 `HC<name>.c/.h`。禁止单次混用 `-H/-C/-P/-N`。
- 本项目用 MVTec 官方 `UseHALCON.cmake` 的 `HALCON_AddExtensionPackage(name DEF_FILES ... SOURCES ...)`，自动生成 hcomp 调用 + 主库/接口库与 help/html。
- 独立库用 `add_subdirectory(<lib>)` 引入，包链接 `<lib>` 目标；全局 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 已指向 `bin/`，DLL 自动落位。
- 产物命名固定：`<包名>.dll`（主库）、`<包名>c.dll`（C 接口）、`<包名>cpp.dll`、`<包名>dotnet.dll`，**不得改名**。
- 激活：`HALCONEXTENSIONS` 加**包目录完整路径**（反斜杠）；PATH 加 `bin/`。改注册表后**重开** HDevelop/终端生效。
- XL：另编一套带 `xl` 后缀的库（`HC_LARGE_IMAGES`），与 halconxl.lib 链接。

## 6. 测试（hrun）

`.hdev` 是 XML 格式（`<hdevelop><procedure name="main"><body><l>...</l></body></procedure></hdevelop>`），含中文必须 **UTF-8 无 BOM**（否则 hrun 按 GBK 解析中文注释把行搅乱）。

```powershell
hrun -v script.hdev          # 跑，失败打印错误行
hrun -d script.hdev          # 跑完 dump 所有变量值（验证输出真被赋值）
hrun -P script.hdev          # pedantic：报告无效行（比 -v 严格，能跑的文件也可能报，仅作参考）
```

- 断言用 `throw('msg')`：**例程退出码 0 才算过**。
- **关键**：检查输出不是空 tuple——空输出的 `if` 断言会假性通过，必须用 `-d` 确认变量有真值。
- 验证环境变量是否生效：用 WMI `Win32_Process Create` 起全新进程（它重新读注册表），别用当前 shell 的子进程（继承旧环境）。
- 对 region 做对照测试：集合代数/补集/fill_up/互转 用「对称差面积=0」判精确一致；补集区域不要用 `area_center` 数值直接比（补集面积语义特殊，可为负），用「再求补后对称差」。

## 7. 新算子速查流程

1. 独立库加 C ABI 函数（句柄式，避免 C++ 类型的 DLL），自测通过。
2. `def/Halcon_<Module>.def` 加算子（遵循 §2），单独 `hcomp -u -H -pcheck <def>` 验证语法（exit=0）。
3. `source/Halcon_<Module>.cpp` 加 `Hcv_<op>`（遵循 §3）。
4. `Halcon_SoftwarePackage.c` 加 `CHcv_<op>` 包装；`Halcon_SoftwarePackage.h` 加声明。
5. 改 `CMakeLists.txt`：`DEF_FILES` 加 def、`EXT_SOURCES` 加 cpp、链接独立库目标。
6. 整包构建 → 加 hdev 例程到 `examples/`（遵循 §6）→ `hrun -v` + `-d` 验证。
7. README 补算子文档（原型/参数/错误码/例程）。

## 参考文件（本项目内）

- 现有可抄的 supply 实现：`source/Halcon_CVRegion.cpp`（region 类）、`source/Halcon_Ransac.cpp`（全控制参数+字符串数值 tuple）、`source/Halcon_Math.cpp`（muparser + Eigen LM、字符串输出 msgOut 模式）。
- 可抄的 DEF：`def/Halcon_CVRegion.def`（含对象+控制+value_number）、`def/Halcon_Ransac.def`（全控制参数）。
- 手册：`extension_package_programmers_manual.pdf`（本仓库根目录；HAllocStringMem §5.5.10、HGetRL/HAllocRLTmp R7、包结构 §1.2.3、错误码 R6）。

## 8. 【血泪教训】改了算子签名后算子“不被调用”（静默跳过、输出为空、无报错）

**根因**：HALCON 做调用校验时，算子的参数个数/签名是从包目录 help/operators_en_US.num（算子签名数据库）读的，**不是**只从 DLL 注册读。该数据库每行 4 个数 = 入对象 出对象 入控制 出控制，按算子注册顺序排列。

**坑**：CMake 构建会用 hcomp 把该数据库重新生成到 **build/help/**，但默认**不会拷贝到包 help/**。于是：

- 改了某个已有算子的签名（加/减参数）→ 只重编了 DLL → build/help 是新的、包 help/ 还是旧签名 → HALCON 按旧签名校验 → 新签名的调用参数个数对不上 → **该行被静默跳过**（不派发、无报错、输出为空）。
- **新加的算子**（旧库里没有）不受影响，按 DLL 注册走，正常派发——这就是“新算子好使、改签名的旧算子不好使”的原因。
- 没改签名的旧算子也好使（旧库签名碰巧匹配）。

**诊断手法**（当某算子“明明注册了却不被调用、无报错”时）：

1. 给 supply 第一行加 `return 30099;`（>10000 自定义错误码）。重编后调用：报 30099 = 被派发；不报错且输出空 = **没被派发**（卡在校验）。
2. 用旧签名（旧参数个数）调用一次：若能派发 → 确认 HALCON 用的是旧签名 → 就是 help 数据库没同步。
3. 对照一个**全新名字**的同签名算子：新名好使 → 进一步确认是按名字取了旧签名。

**解决（已固化进 CMakeLists）**：主库 POST_BUILD 加了 `copy_directory build/help -> help`。
**每次改 DEF 后必须整包重建**（让 hcomp 重新生成数据库并触发该拷贝）。手动补救：`Copy-Item build\help\operators_en_US.* help\ -Force`。

**另注意**：`cmake --build build --target Halcon_SoftwarePackage` 只重编主库；`<包名>c.dll`、`cpp.dll`、`dotnet.dll` 是独立目标（`...cint/...cppint/...dotnetint`），HALCON 会把它们都加载。**改代码后要 `cmake --build build --config Release`（不带 `--target`）整包重建**，否则接口变体过期。
