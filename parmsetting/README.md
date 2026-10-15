# 参数配置模块 parm_ui

给一段参数 JSON 字符串 + 当前用户权限等级，**弹出一个模态窗口**（界面按 `Type` 字段自动生成），
关窗后函数返回**改完的 JSON**。

```
调用方线程 ──调用──▶ parm_ui_json(json, 权限等级)
                      │  内部起一条独立 UI 线程：建窗 → 渲染 → 事件循环 → 销毁
                      │  宿主其它线程完全不受影响
             ◀─返回──  改完的 JSON（或原 JSON = 用户取消）
```

* 纯 **CMake** 工程，不生成 / 不依赖任何 IDE 工程文件
* Windows 用 **MSVC**，Linux 用 **GCC**
* 三方库全部开源非商业：**Dear ImGui v1.92.9b**(MIT) + **GLFW 3.4**(zlib/libpng)，静态链进本库并隐藏符号
* 只用到工作区自带的 `json/json.hpp`（nlohmann 3.6.1）做 JSON 解析与转义

---

## 1. 目录结构

```
include/parm_ui.h      对外唯一的 C 头文件（唯一需要给使用方的东西）
src/parm_ui.cpp        C ABI 实现：线程模型、会话锁、错误信息
src/ui_app.cpp         GLFW + ImGui 窗口会话（模态循环、字体、截图钩子）
src/ui_view.cpp        参数树渲染：按 Type 选控件 + 权限 + 校验弹窗
src/ui_font.cpp        跨平台中文字体探测 + 字形范围构建
src/fs_dialog.cpp      内置文件/目录选择对话框（ImGui 实现，跨平台）
src/param_model.*      纯模型层：解析 / Value 归一 / Limit 解析 / 校验 / 回写
src/ordered_json.*     保序 JSON DOM（用 nlohmann 的 SAX 解析回调自建）
demo/main.cpp          命令行 demo
tests/selftest.cpp     无头自测（42 项断言）
test/sample_all_types.json  覆盖全部 12 种数据类型的样例（含各种坏节点）
scripts/               构建 / 自测 / 冒烟脚本
```

## 2. 构建

**Windows（MSVC + Ninja）**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-msvc.ps1
# 或者直接用 VS Code 的 CMake Tools（CMakePresets.json 里已有 windows-msvc 预设）
```

脚本会用 `vswhere` 找到带 MSVC 的 Visual Studio（2019/2022 都行）、进入 x64 开发环境、
用 VS 自带的 Ninja 构建。产物：`build/msvc/bin/parm_ui.dll` + `parm_ui_demo.exe`。

**Linux（GCC）**

```bash
sudo apt install build-essential cmake ninja-build git \
     libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
bash scripts/build-linux.sh          # 产物：build/gcc/bin/libparm_ui.so
```

> 需要 GLFW 的 Wayland 后端时加 `-DPARM_UI_WAYLAND=ON`（要装 `libwayland-dev libxkbcommon-dev wayland-protocols`）。
> 默认走 X11（Wayland 桌面上通过 XWayland 也能用）。
> 首次配置会联网拉取 GLFW / ImGui；离线环境用 `-DPARM_UI_FETCH_DEPS=OFF`，
> 并把 `glfw/`、`imgui/` 源码放到 `third_party/`。

## 3. C 接口

```c
#include <parm_ui.h>

int   cancelled = 0;
char* out = parm_ui_json(in_json, /*当前用户权限等级*/ 3, &cancelled);
if (out) { /* 用结果 */ parm_ui_free(out); }
else     { printf("%s\n", parm_ui_last_error()); }
```

| 函数 | 说明 |
|---|---|
| `parm_ui_json(in_json, competence, *cancelled)` | **默认入口**。内部起 UI 线程，**任意线程可调**，只有当前调用线程阻塞到关窗 |
| `parm_ui_json_inline(...)` | 不另起线程，就在当前线程里跑整个 UI 循环（调用方保证"同一线程跑完全程"） |
| `parm_ui_json_async(in_json, competence, cb, user)` | 立刻返回，窗口关闭时回调（回调跑在 UI 线程上，参数指针仅回调期间有效） |
| `parm_ui_free(p)` | 释放返回值（内部用 `malloc`，所以 `free()` 也能释放） |
| `parm_ui_last_error()` | 本线程最近一次错误原因（thread-local） |
| `parm_ui_version(&maj,&min,&pat)` | 版本号 |

返回值语义：

| 情况 | 返回 | `*cancelled` |
|---|---|---|
| 点"保存并关闭"（校验通过） | 修改后的 JSON | 0 |
| 点"取消" / 右上角 × / 关窗 | **原 JSON 的副本** | 1 |
| 失败（入参非法 / JSON 语法错误 / 无图形环境 / 初始化失败） | `NULL` | 0 |

**线程约定**：`parm_ui_json()` 起的是"本库自己的 UI 线程"，所以宿主是不是主线程都无所谓，
宿主其它线程（图像采集、算法、日志…）也不会被冻结——这正是 GLFW 要求的"初始化→建窗→
事件循环→销毁，全程同一条线程"。同一时刻只允许一个配置窗口，后到的调用会排队。

## 4. 配置 JSON 规范

层级结构：含 `Type` 的节点是末端参数，不含的是分组（可无限嵌套）。

```jsonc
{
  "分组A": {
    "参数1": { "Competence": 1, "Type": "int",   "Limit": [0, 100], "Value": 60 },
    "子分组": {
      "参数2": { "Competence": 2, "Type": "string_enum", "Limit": ["CPU","GPU"], "Value": "GPU" }
    }
  }
}
```

| Type | Limit | UI 控件 |
|---|---|---|
| `int` / `float` | `[min, max]`（闭区间） | 数字输入框 + 滑块 + 右侧范围提示 |
| `bool` | `""` | 复选框（值写回字符串 `"TRUE"` / `"FALSE"`） |
| `string` | `""` | 单行文本框 |
| `string_enum` / `int_enum` / `float_enum` | `["a","b",...]` / `[1,2,4,8]` | 只读下拉框 |
| `int[]` / `float[]` | `[min, max]`（作用到**每个元素**） | 多行输入框 + 每行 `-` + 底部 `+ 添加` |
| `string[]` | `""` | 同上 |
| `directory` | `""` | 文本框 + "浏览目录…"（内置选择器） |
| `path` | `""` | 文本框 + "浏览文件…" |

* `Competence`：0 操作员 / 1 产线工程师 / 2 系统工程师 / 3 管理员。节点等级 ≤ 当前等级才可编辑，
  否则**置灰只读**；保存时二次校验，越权节点的值**不会**被写回。
* 数组新增项的默认值：数字取 Limit 下限，字符串取空串。

## 5. 关键行为

**保真（需求 2.1）**
* 键顺序、未知字段、注释字段全部原样保留（用自建的保序 DOM，只把编辑过的 `Value` 写回去）。
* 浮点数字面量保留原文（`1.0e2` 不会被改写成 `100.0`），只有被 UI 改过的值才重新格式化。
* 输出缩进跟随输入风格：机器生成的单行紧凑 JSON 仍旧紧凑输出，多行文件用 2 空格缩进。

**健壮性（需求 3）**
* 单个节点解析失败只记一条诊断并跳过（不渲染、不改动、原样保留），整棵树照常加载。
* 只有 JSON 语法错误、根不是对象、空输入才算整体失败。
* 顶部提示条显示"N 个节点已跳过 / M 条提示"，点"查看详情"能看到每条诊断的完整路径与原因。
* 缺 `Type` 但没有 `Competence/Limit/Value` 的节点按"分组"处理；只要含这三个键之一，
  就判定为"漏写 Type 的参数"，给一条精准诊断而不是把它下面的标量逐个报错。

**校验（需求 2.3）**
* 数字越界、枚举值不在候选列表 → 阻断保存，弹窗逐条列出（含完整路径）。
* `directory` / `path` 不存在 → 只提示，可"仍然保存"。
* 只读节点不参与校验（用户没权限改，否则会永远存不下去）。

## 6. 与需求文档的对应

| 需求 | 实现 |
|---|---|
| 1.1 层级结构 / 1.2 四字段 | `src/param_model.cpp` 建树规则 |
| 1.3 12 种类型 | 全部实现，样例 `test/sample_all_types.json` 覆盖 |
| 1.4 权限等级 | 置灰只读 + 保存时不越权写回 |
| 2.1 读写 / 保真 / 错误 | 保序 DOM + 诊断列表 |
| 2.2 自动渲染 | `src/ui_view.cpp` 的控件映射表 |
| 2.3 输入校验 | 滑块/下拉即时约束 + 保存前全量校验 |
| 2.4 权限控制 | 入参 `competence`，不自己管登录 |
| 2.5 多配置文件 / 2.6 多语言 / 3 追溯 | **本期未做**（按约定） |

## 7. 测试

**无头自测**（纯模型层，42 项断言，不需要显示器）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run-selftest.ps1
# Linux: ./build/gcc/bin/parm_ui_selftest ./test/sample_all_types.json
```

覆盖：键顺序/未知字段/数字字面量保真、节点级健壮性、Value 归一、整体性错误、
校验与权限（只读节点不校验不回写）、数组逐元素校验、真实样例往返。

**界面冒烟 + 截图**（会真的弹窗 2 秒再自动关掉；截图存 `build/smoke.bmp`）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -Png
powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -Competence 1          # 看只读置灰
powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -ClickSaveFrame 20 -Png # 看校验失败弹窗
powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -InputFile 参数_out.json -SaveOnly
```

测试钩子（只是环境变量，正常运行完全不受影响）：

| 变量 | 作用 |
|---|---|
| `PARM_UI_SMOKE_MS` | 窗口显示 N 毫秒后自动关闭（默认等同"取消"） |
| `PARM_UI_SMOKE_SAVE=1` | 自动关闭时走"保存并关闭"分支 |
| `PARM_UI_SMOKE_SHOT=file.bmp` | 第 3 帧截图 |
| `PARM_UI_SMOKE_CLICK_SAVE=N` | 第 N 帧模拟点击"保存并关闭"（用来截校验弹窗） |
| `PARM_UI_SMOKE_SHOT2=file.bmp` | 点击之后第 3 帧截图 |
| `PARM_UI_FONT=/path/font.ttc` | 强制指定中文字体（相对／绝对路径都可以） |

**中文字体与 UTF-8**：字体按 **相对路径优先** 探测（`fonts/`、`resources/fonts/`、`assets/fonts/`、`./`），
找不到再退到系统字体（Windows 微软雅黑/黑体/宋体，Linux Noto Sans CJK/文泉驿）。
字形用的是 **ImGui 1.92 的按需加载**：不需要预置字形范围，**任意 UTF-8 字符**
（中文、生僻字、甚至补充平面的表情）用到时才从字体文件里取；字体本身没那个字形才会显示成 `?`。
一个字体都找不到时会退回内置英文字体，并在窗口顶部提示可用 `PARM_UI_FONT` 指定。

## 8. 已知取舍
* **路径尽量用相对路径**：不往界面/日志里打绝对路径；文件选择器选完之后，
  只要目标在进程当前工作目录下面，就回填相对路径（跑出去了才用绝对路径）；
  字体也是先在 `fonts/` 这类相对目录里找。想改这个行为看 `src/fs_dialog.cpp` 的 `prefer_relative()`。* `bool` 的 `Value` 按需求表用**字符串** `"TRUE"` / `"FALSE"`（不是 JSON 布尔）。加载时也接受 JSON `true/false`，会归一成上面的字符串。
* 枚举的当前值不在 `Limit` 列表里时：加载给一条提示，下拉框把当前值补成一项（标红），**不静默改值**。
* 输出缩进跟随输入（见 §5）；如需固定 2 空格缩进，改 `src/ui_app.cpp` 的 `detect_indent()`。
* 同一进程同一时刻只允许一个配置窗口（配置界面本身是"一次改一份"的语义）。
* Linux 下没有 `DISPLAY` / `WAYLAND_DISPLAY` 时直接返回错误码，不做离屏渲染。
* `float` 在界面上按 `%.10g` 显示/输入（配置量级足够；原文件里的字面量在不改动的节点上原样保留）。
