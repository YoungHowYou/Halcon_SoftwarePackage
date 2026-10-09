"""cv_region —— HALCON 语义的游程(RLE) region 库 ``cvr_core`` 的 Python 绑定。

C++ 侧只有一个头 ``cvr/cvr.hpp`` 和一个实现 ``cvr.cpp``（纯 STL、C++17、
零三方依赖、与 HALCON 无关），本包把它编译成扩展模块 ``cv_region._cvr``。

用法速览::

    import numpy as np
    import cv_region as cvr
    from cv_region import Region, Run

    # 1) 直接从二值图/mask 拿 region，再拆连通域、按面积筛选
    reg = Region.from_mask(np.array([[0, 1, 1],
                                     [0, 1, 0]], dtype=np.uint8))
    comps = reg.connection(8, 3, 2)                       # w, h
    big = cvr.cvr_select_shape(comps, ["area"], "and", [2.0], [1e30])

    # 2) 特征
    print(reg.area_center())                              # (row, col, area)
    print(reg.get_feature("contlength"))

    # 3) 与 numpy 互转
    mask = reg.to_mask(3, 2)                              # uint8, 前景 255

命名约定（与 C++ 头文件逐条对应，便于对照）：

* **模块级函数保持 C++ 原名**：``cvr_union2`` / ``cvr_connection`` /
  ``cvr_select_shape`` …；C++ 的出参在 Python 里变成返回值（tuple / Region /
  list[Region]）。
* 单区域算子同时挂到 :class:`Region` 上做方法，方法名 = 函数名去掉 ``cvr_``
  以及紧随的 ``region_`` / ``feature_`` 前缀，例如
  ``cvr_region_normalize`` → :meth:`Region.normalize`、
  ``cvr_feature_area_center`` → :meth:`Region.area_center`、
  ``cvr_region_to_bin`` → :meth:`Region.to_bin`。
* C++ 侧「返回 bool + ``cvr_last_error()`` 取错误串」在此统一转成
  ``RuntimeError``（消息即 ``cvr_last_error()``）。

依赖：核心功能只需要标准库（buffer 协议，numpy 数组 / memoryview /
bytearray 都收）；``to_image`` / ``to_mask`` / ``from_image(depth=None)``
需要 numpy。
"""

from . import _cvr
from ._cvr import *  # noqa: F401,F403  (扩展模块导出的全部名字)

__all__ = [name for name in dir(_cvr) if not name.startswith("_")]

try:  # 版本取自安装元数据
    from importlib.metadata import PackageNotFoundError, version as _version

    try:
        __version__ = _version("cv-region")
    except PackageNotFoundError:  # 未安装（如源码目录直用）
        __version__ = "0.0.0+unknown"
except Exception:  # pragma: no cover - Python < 3.8 或不完整环境
    __version__ = "0.0.0+unknown"
