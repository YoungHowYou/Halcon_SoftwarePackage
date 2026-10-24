"""yolo_core —— YOLO26 实例分割（OpenVINO）的 Python 绑定。

C++ 侧是 ``yolo_core/``（``include/yolo/yolo.hpp`` + ``src/yolo.cpp``，与 HALCON 无关），
本包把它编译成扩展模块 ``yolo_core._yolo``；HALCON 扩展包里的两个算子
``OpenvinoLoadModel`` / ``yolo_seg_detect`` 调的也是同一份实现。

用法速览::

    import numpy as np
    import yolo_core

    model = yolo_core.Model("best.xml", "CPU")      # .bin 需在同目录同名
    print(model.input_size)                          # (640, 640)

    img = ...                                        # uint8，(H,W) 灰度 或 (H,W,3) BGR
    r = model.detect(img, conf_threshold=0.15, overlap=0.2)

    r.num                                            # 检测数量
    r.boxes                                          # (N,4) int32 [x1,y1,x2,y2]
    r.scores, r.class_ids
    r.label_image                                    # (H,W) uint16，0=背景，值=class_id+1
    r.masks                                          # list[(h,w) uint8]，框内二值掩膜(0/255)

推理口径（与 HALCON 侧完全一致）：

* **不缩放**：图小于窗口（默认 = 模型输入尺寸）时左上对齐补 0，图大于窗口时按重叠度滑窗；
* 重叠度默认 **0.2**（640 窗口 → 步长 512），可传 ``overlap=`` 或用 ``step=`` 显式指定；
* 去重：窗口内 raw 输出用 IoU-NMS，跨窗口用 **IoS**（交集/较小框面积，仅同类比较）；
* 框坐标、标签图都换算回**原图**坐标系/原图尺寸。

注意：

* 输入必须是 **uint8**（灰度或 BGR），形状 ``(H,W)`` / ``(H,W,3)``；
* 一个 :class:`Model` 只持有一个 ``ov::InferRequest``，**不能**多线程同时调用它
  （推理时已释放 GIL，要并行就每个线程建一个 Model）；
* 失败抛 :class:`RuntimeError`，消息里带 OpenVINO 原文（例如模型路径写错时的
  ``ir: Could not open the file``）；
* 本包**不含** OpenCV / OpenVINO 的 DLL：运行前需让 ``opencv_*4.dll`` / ``openvino*.dll``
  可被找到 —— 设置环境变量 ``YOLO_CORE_DLL_DIR``，或把它们放进 ``PATH`` / 包目录。
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Tuple, Union

import numpy as np

__all__ = ["Model", "DetectResult", "dll_directories"]


# ---------------------------------------------------------------------------
# DLL 搜索（Windows）
#   本包只带 .pyd，OpenCV / OpenVINO 的运行时 DLL 需要能被找到。依次尝试：
#     1) 环境变量 YOLO_CORE_DLL_DIR
#     2) 包目录自身（把 DLL 手动拷进 yolo_core/ 也可以）
#     3) 源码树：<repo>/build/vcpkg_installed/x64-windows/bin
#        （从包位置 / 当前工作目录逐级往上找，便于在仓库里直接 pytest）
# ---------------------------------------------------------------------------
def _dll_candidates() -> List[Path]:
    cands: List[Path] = []
    env = os.environ.get("YOLO_CORE_DLL_DIR")
    if env:
        cands.extend(Path(p) for p in env.split(os.pathsep) if p)
    here = Path(__file__).resolve().parent
    cands.append(here)
    cands.append(here / "_dll")
    roots = list(here.parents) + list(Path.cwd().resolve().parents) + [Path.cwd().resolve()]
    for root in roots:
        cands.append(root / "build" / "vcpkg_installed" / "x64-windows" / "bin")
    return cands


def _bootstrap_dll_dirs() -> List[str]:
    if sys.platform != "win32":
        return []          # 类 Unix 靠 LD_LIBRARY_PATH / rpath
    added: List[str] = []
    seen = set()
    for cand in _dll_candidates():
        try:
            key = str(cand.resolve())
        except OSError:
            continue
        if key in seen or not cand.is_dir():
            continue
        seen.add(key)
        try:
            os.add_dll_directory(key)
            added.append(key)
        except OSError:
            pass
    return added


dll_directories: List[str] = _bootstrap_dll_dirs()
"""本包 import 时为查找 OpenCV / OpenVINO DLL 而注册的目录（Windows 才有）。"""

try:
    from . import _yolo                      # noqa: F401  (先注册 DLL 目录再导入扩展)
    from ._yolo import Model as _Model
except ImportError as exc:                    # pragma: no cover - 环境问题
    raise ImportError(
        "导入 yolo_core._yolo 失败（多半是找不到 OpenCV / OpenVINO 的运行时 DLL）。\n"
        "请把 vcpkg 的 bin 目录加入 PATH，或设置环境变量 YOLO_CORE_DLL_DIR 指向它，例如：\n"
        "    set YOLO_CORE_DLL_DIR=<repo>\\build\\vcpkg_installed\\x64-windows\\bin\n"
        f"原始错误：{exc}"
    ) from exc


@dataclass
class DetectResult:
    """``detect()`` 的结果（全部 numpy 化）。"""

    num: int
    """检测数量。"""

    boxes: np.ndarray
    """``(N, 4) int32``，``[x1, y1, x2, y2]``（原图像素；右/下为开区间）。"""

    scores: np.ndarray
    """``(N,) float32`` 置信度。"""

    class_ids: np.ndarray
    """``(N,) int32`` 类别 ID。"""

    label_image: np.ndarray
    """``(H, W) uint16``（与原图等大）：``0`` = 背景，像素值 = 类别 ID + 1。"""

    masks: List[np.ndarray]
    """``N`` 个 ``(h, w) uint8`` 框内二值掩膜（0 / 255）。"""

    def __len__(self) -> int:
        return int(self.num)

    def __repr__(self) -> str:
        if self.num == 0:
            return "DetectResult(num=0)"
        ids = ", ".join(f"{int(c)}:{float(s):.3f}" for c, s in zip(self.class_ids, self.scores))
        return f"DetectResult(num={self.num} [{ids}])"


class Model(_Model):
    """:class:`yolo_core._yolo.Model` 的 Python 友好封装（``detect`` 返回 :class:`DetectResult`）。"""

    def detect(
        self,
        image: np.ndarray,
        *,
        conf_threshold: float = 0.25,
        mask_threshold: float = 0.5,
        nms_threshold: float = 0.45,
        overlap: float = 0.2,
        step: Optional[Union[int, Sequence[int]]] = None,
        window: Optional[Union[int, Sequence[int]]] = None,
    ) -> DetectResult:
        """检测 / 实例分割。

        :param image: ``uint8`` 数组，灰度 ``(H,W)`` 或 BGR ``(H,W,3)``。
        :param conf_threshold: 置信度阈值（默认 0.25）。
        :param mask_threshold: 掩膜二值化阈值（默认 0.5）。
        :param nms_threshold: 去重阈值（默认 0.45）：窗口内按 IoU，跨窗口按 IoS。
        :param overlap: 滑窗重叠度（默认 0.2 → 640 窗口步长 512）。
        :param step: 显式滑窗步长（int 或 ``(sx, sy)``），给了就优先于 ``overlap``。
        :param window: 窗口尺寸（int 或 ``(w, h)``），默认 = 模型输入尺寸。
        """
        raw = super().detect(
            image,
            conf_threshold=conf_threshold,
            mask_threshold=mask_threshold,
            nms_threshold=nms_threshold,
            overlap=overlap,
            step=step,
            window=window,
        )
        return DetectResult(**raw)

    @property
    def input_size(self) -> Tuple[int, int]:
        """模型输入尺寸 ``(width, height)``。"""
        return int(self.input_width), int(self.input_height)


try:  # 版本取自安装元数据
    from importlib.metadata import PackageNotFoundError, version as _version

    try:
        __version__ = _version("yolo-core")
    except PackageNotFoundError:  # 未安装（如源码目录直用）
        __version__ = "0.0.0+unknown"
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"
