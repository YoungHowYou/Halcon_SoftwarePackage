"""yolo_core Python 绑定测试。

运行（**cwd 必须是仓库根**，否则 `import yolo_core` 可能解析到 `yolo_core/python/yolo_core`
这个源码目录而不是装好的包，扩展模块 `_yolo` 找不到）：:

    .venv\\Scripts\\python.exe -m pytest yolo_core/python/tests

模型权重 `examples/yolo/best.bin` 不入库（111 MB）；没有模型时整个文件 skip。
"""

from pathlib import Path

import numpy as np
import pytest

import yolo_core

REPO = Path(__file__).resolve().parents[3]        # yolo_core/python/tests -> <repo>
MODEL = REPO / "examples" / "yolo" / "best.xml"

pytestmark = pytest.mark.skipif(
    not (MODEL.exists() and MODEL.with_suffix(".bin").exists()),
    reason="需要 examples/yolo/best.xml + best.bin（权重不入库）",
)


@pytest.fixture(scope="module")
def model():
    return yolo_core.Model(str(MODEL), "CPU")


def test_input_size(model):
    assert model.input_size == (640, 640)
    assert (model.input_width, model.input_height) == (640, 640)


def test_detect_single_window(model):
    img = np.full((640, 640), 128, np.uint8)      # 与窗口等大：单窗口、不 pad
    r = model.detect(img)
    assert isinstance(r, yolo_core.DetectResult)
    assert r.num == len(r)
    assert r.boxes.shape == (r.num, 4) and r.boxes.dtype == np.int32
    assert r.scores.shape == (r.num,) and r.scores.dtype == np.float32
    assert r.class_ids.shape == (r.num,) and r.class_ids.dtype == np.int32
    assert r.label_image.shape == (640, 640) and r.label_image.dtype == np.uint16
    assert len(r.masks) == r.num
    if r.num:
        assert (r.scores >= 0.25).all()
        assert (r.boxes[:, 2] > r.boxes[:, 0]).all()
        assert (r.boxes[:, 3] > r.boxes[:, 1]).all()
        assert r.label_image.max() >= 1            # 有前景像素（class_id + 1）


def test_pad_small_image(model):
    """小于窗口的图：pad 到窗口尺寸、不缩放；结果仍按原图尺寸给出。"""
    img = np.full((300, 400), 180, np.uint8)
    r = model.detect(img)
    assert r.label_image.shape == (300, 400)
    if r.num:
        assert (r.boxes[:, 2] <= 400).all() and (r.boxes[:, 3] <= 300).all()


def test_multiwindow_overlap(model):
    """900x900 → 按重叠度滑窗（默认 0.2）；标签图仍是原图尺寸。"""
    img = np.zeros((900, 900), np.uint8)
    r = model.detect(img, overlap=0.2)
    assert r.label_image.shape == (900, 900)
    if r.num:
        assert (r.boxes[:, 2] <= 900).all() and (r.boxes[:, 3] <= 900).all()


def test_bgr_input(model):
    img = np.full((640, 640, 3), 64, np.uint8)
    r = model.detect(img)
    assert r.label_image.shape == (640, 640)


def test_explicit_step_and_window(model):
    img = np.zeros((800, 800), np.uint8)
    r = model.detect(img, window=512, step=(256, 256))
    assert r.label_image.shape == (800, 800)


def test_reject_non_uint8(model):
    with pytest.raises(TypeError):
        model.detect(np.zeros((640, 640), np.float32))


def test_reject_bad_shape(model):
    with pytest.raises((ValueError, TypeError)):
        model.detect(np.zeros((640,), np.uint8))


def test_bad_model_path_raises_with_reason():
    """错误原因要能透到 Python（OpenVINO 原文）。"""
    with pytest.raises(RuntimeError) as ei:
        yolo_core.Model(str(REPO / "no_such_model.xml"), "CPU")
    msg = str(ei.value).lower()
    assert ("could not open" in msg) or ("no_such_model" in msg)
