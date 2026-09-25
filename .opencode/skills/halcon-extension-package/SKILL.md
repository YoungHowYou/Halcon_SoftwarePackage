---
name: halcon-extension-package
description: 'Use when creating, modifying, debugging, or testing HALCON extension package operators (.def files, supply layer, hcomp, Halcon_SoftwarePackage, cv_region static lib (region + ransac)), or when a HALCON operator fails to load, is silently skipped, crashes, or returns empty/wrong results, or when hrun/hdev examples misbehave. Covers DEF syntax rules, supply C/C++ conventions, CMake build, help database sync, and hrun testing.'
---

# HALCON Extension Package — 项目专用技能

## 1. 架构与目录

```
cv_region/                          → 算法本体（C++17，纯 STL）：region 运算 + ransac 拟合
  ├─ 静态库 cvr_core，直接链入扩展包，supply 直调 C++ API（免跨 DLL 拷贝）
  └─ Eigen/muparser 经 cvr_core PUBLIC 链入（ransac 表达式模型与 LM 求解）

Halcon_SoftwarePackage/             → 扩展包（沿用现有包，勿新建）
  ├── def/Halcon_<Module>.def       → 算子注册（每模块一个文件）
  ├── source/Halcon_<Module>.cpp    → supply 层（参数读取 + 调核心 + 输出）
  ├── source/Halcon_SoftwarePackage.c → CH<op> 包装函数（H<op> → supply）
  └── include/Halcon_SoftwarePackage.h → H<op> 声明
```

**三层命名**（缺一不可，名字必须对应）：DEF 物理名 `CHcv_union2`（`cv_union2<- CHcv_union2[...]`）→ 包装函数 `CHcv_union2`（.c 中 `return Hcv_union2(ph);`）→ 实现函数 `Hcv_union2`（模块 cpp；OpenCV 模块实现函数带 C 前缀 `HCcv_xxx`，对应 `Ccv_xxx`）。

**参数全开放约定**：算子可调参数尽量全开放（默认值与底层库一致）。图像/滤波/矩阵类用控制参数按位传满；特征检测/匹配/变换估计类（orb/sift/akaze/bf_knn/affine_partial/rigid）用 dict 键传入，未设键取默认。

## 2. DEF 文件规则（出错率最高的地方）

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

## 5. 构建

- hcomp 分两次调用：`-H` 生成包初始化 `H<name>.c`（-p<包名>，多个 DEF 一次调用）；`-C` 生成 C 接口 `HC<name>.c/.h`。
- 本项目用 MVTec 官方 `UseHALCON.cmake` 的 `HALCON_AddExtensionPackage(name DEF_FILES ... SOURCES ...)`，自动生成 hcomp 调用 + 主库/接口/help/html 目标。
- 改 DEF 后必须 `cmake --build build --config Release`（不带 --target）整包重建——接口变体 c/cpp/dotnet 是独立目标；POST_BUILD 会同步 help 数据库到包 help/。
- 产物命名固定：`<包名>.dll`、`<包名>c.dll`、`<包名>cpp.dll`、`<包名>dotnet.dll`，不得改名。
- 激活：`HALCONEXTENSIONS` = 包目录完整路径（反斜杠）；改注册表后重开 HDevelop/终端生效。
- HTML 帮助目标（`..._html`）增量构建不可靠（MSB8065，改了 DEF 也可能不重生成）：手动全量——在 `build/doc/html/reference` 下跑 `hcomp -u -R -p<包名> -len_US <全部 def>`，再 `Copy-Item build/doc/html/* doc/html/ -Recurse -Force`（同理 help db：`Copy-Item build/help/operators_en_US.* help/ -Force`）。

## 6. 测试（hrun）

```powershell
hrun -v script.hdev    # 跑，失败打印错误
hrun -d script.hdev    # 跑完 dump 所有变量值（验证输出真被赋值）
```

- .hdev 是 XML（`<hdevelop><procedure name="main"><body><l>...</l></body></procedure></hdevelop>`），含中文必须 UTF-8 BOM；文本里的 `<` 要转义 `&lt;`。
- 断言用 `throw('msg')`；例程退出码 0 才算过。
- **关键**：空输出会让 `if` 断言假性通过，必须 `-d` 确认变量有真值。
- 用 WMI `Win32_Process Create` 起全新进程验证环境变量（别用当前 shell 子进程）。

## 7. 新算子速查流程

1. 核心库加 C++ 函数（cvr 内直接加；跨模块才用句柄式 ABI），自测通过。
2. `def/Halcon_<Module>.def` 加算子（遵循 §2），单独 `hcomp -u -H -pcheck <def>` 验证语法（exit=0）。
3. `source/Halcon_<Module>.cpp` 写 `Hcv_<op>`（遵循 §3）。
4. `Halcon_SoftwarePackage.c` 加 `CHcv_<op>` 包装；`Halcon_SoftwarePackage.h` 加声明。
5. 整包构建 + 写 .hdev 例程到 `examples/` → `hrun -v` + `-d` 验证。
6. README 补算子文档（原型/参数/错误码/例程）。

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

- 现有可抄的 supply：`source/Halcon_CVRegion.cpp`（region 类）、`source/Halcon_Ransac.cpp`（全控制参数+字符串+数值 tuple）、`source/Halcon_Math.cpp`（muparser + Eigen LM、字符串输出 msgOut 模式）。
- 可抄的 DEF：`def/Halcon_CVRegion.def`（对象+控制+value_number）、`def/Halcon_Ransac.def`（全控制参数）。
- 手册：`extension_package_programmers_manual.pdf`（本仓库根目录；HAllocStringMem §5.5.10、HGetRL/HAllocRLTmp R7、包结构 §1.2.3、错误码 R6）。
