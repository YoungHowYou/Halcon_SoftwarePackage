# yolo-core（Python 包）

`yolo_core/` 这套 YOLO26 实例分割推理（OpenVINO）的 Python 绑定，结构照 `cv_region/python`：
pybind11 扩展模块 `yolo_core._yolo` + 薄 Python 层，**直接编译上级 `src/yolo.cpp`**，不复制源码。
HALCON 扩展包里的 `OpenvinoLoadModel` / `yolo_seg_detect` 用的是同一份实现，口径完全一致。

## 安装

wheel 里只含 `.pyd` + Python 代码，**不含** OpenCV / OpenVINO 的 DLL；构建时需要它们的 CMake 配置，
运行时要能找到它们的 DLL。

```powershell
# 1) 构建 + 安装进当前 Python 环境（这里是仓库根的 .venv）
.\.venv\Scripts\python.exe -m pip install --no-build-isolation .\yolo_core\python `
    --config-settings=cmake.define.CMAKE_PREFIX_PATH=d:/desk/source/Halcon_Extension/Halcon_SoftwarePackage/build/vcpkg_installed/x64-windows

# 2) 出 wheel（同样要带上面的 --config-settings）
.\.venv\Scripts\python.exe -m pip wheel --no-build-isolation .\yolo_core\python -w .\yolo_core\python\dist `
    --config-settings=cmake.define.CMAKE_PREFIX_PATH=d:/desk/source/Halcon_Extension/Halcon_SoftwarePackage/build/vcpkg_installed/x64-windows
```

运行时的 DLL 查找顺序（`yolo_core/__init__.py` 里注册 `os.add_dll_directory`）：

1. 环境变量 `YOLO_CORE_DLL_DIR`（可多个，用 `;` 分隔）
2. 包目录 `site-packages\yolo_core\`（把 DLL 拷进来也行）
3. 源码树 `<repo>\build\vcpkg_installed\x64-windows\bin`（从包位置 / 当前目录往上找）

⇒ 在仓库里直接从源码用 `pytest` 是能自动找到的；装到别的机器上就设 `YOLO_CORE_DLL_DIR`：

```powershell
$env:YOLO_CORE_DLL_DIR = '<repo>\build\vcpkg_installed\x64-windows\bin'
```

## 用法

```python
import numpy as np
import yolo_core

model = yolo_core.Model("best.xml", "CPU")     # .bin 需同目录同名；device: CPU / GPU / NPU
print(model.input_size)                        # (640, 640)

img = ...                                       # uint8，(H,W) 灰度 或 (H,W,3) BGR
r = model.detect(img, conf_threshold=0.15, overlap=0.2)

r.num                                           # 检测数
r.boxes                                         # (N,4) int32 [x1,y1,x2,y2]（原图像素，右/下开区间）
r.scores                                        # (N,) float32
r.class_ids                                     # (N,) int32
r.label_image                                   # (H,W) uint16，0=背景，值=class_id+1
r.masks                                         # list[(h,w) uint8]，框内二值掩膜 0/255
```

`detect()` 的关键字参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `conf_threshold` | 0.25 | 置信度阈值 |
| `mask_threshold` | 0.5 | 掩膜二值化阈值 |
| `nms_threshold` | 0.45 | 窗口内 IoU-NMS / 跨窗口 IoS 的阈值 |
| `overlap` | 0.2 | 滑窗重叠度（640 窗口 → 步长 512） |
| `step` | `None` | 显式步长，`int` 或 `(sx, sy)`，优先于 `overlap` |
| `window` | `None` | 窗口尺寸，`int` 或 `(w, h)`，默认 = 模型输入尺寸 |

推理口径（与 HALCON 侧一致）：**不缩放**；小图左上补 0 到窗口尺寸；大图按重叠度滑窗；
去重 = 窗口内 IoU + 跨窗口 IoS（仅同类）；坐标/标签图都回到原图坐标系与原图尺寸。

## 测试

```powershell
# cwd 必须是仓库根（否则会 import 到源码目录里的同名包）
.\.venv\Scripts\python.exe -m pytest yolo_core\python\tests
```

模型权重 `examples/yolo/best.bin` 不入库（111 MB，超过 GitHub 单文件限制）；
没有 `best.xml + best.bin` 时整个测试文件 skip。

## 备注

* 一个 `Model` 只持有一个 `ov::InferRequest`：**不要**多线程同时调用同一个实例；
  推理期间释放了 GIL，要并行就每个线程建一个 `Model`。
* 输入必须是 `uint8`；传 `float` 会 `TypeError`（不当静默截断）。
* 失败抛 `RuntimeError`，消息里带 OpenVINO 原文（例如路径写错时的 `ir: Could not open the file`）。
