"""cv_region Python 绑定测试。

设计：关键数值与 C++ 侧测试（cv_region/tests/test_m1..m10）用**同一套期望值**，
所以这里全绿 = Python 绑定与 C++ 实现逐值一致，而不只是"能 import"。
"""

import math

import numpy as np
import pytest

import cv_region as cvr
from cv_region import GrayDepth, Region, Run


# ---------------------------------------------------------------------------
# 辅助：与 C++ 测试同口径的矩形构造
# ---------------------------------------------------------------------------
def rect(r1, c1, r2, c2):
    """实心矩形 [r1..r2] x [c1..c2]（每行一条 run）"""
    return Region([(r, c1, c2) for r in range(r1, r2 + 1)])


# ---------------------------------------------------------------------------
# 模块 / 构造
# ---------------------------------------------------------------------------
def test_module_surface():
    assert isinstance(cvr.__version__, str)
    for name in ("cvr_union2", "cvr_connection", "cvr_select_shape",
                 "cvr_feature_area_center", "cvr_bin_to_region", "Region", "Run"):
        assert hasattr(cvr, name), name


def test_construct_from_tuples_and_runs():
    a = Region([(0, 0, 4), Run(1, 0, 4)])
    assert a.num_runs == 2 and len(a) == 2
    assert a.runs[0] == Run(0, 0, 4)
    assert tuple(a.runs[1]) == (1, 0, 4)
    assert a.runs[0].length == 5
    assert repr(a) == "Region(runs=2, is_compl=False)"
    assert repr(a.runs[0]) == "Run(r=0, cb=0, ce=4)"


def test_runs_setter_and_equality():
    r = Region()
    assert not r
    r.runs = [(0, 1, 2), (1, 1, 2)]
    assert r.num_runs == 2
    assert r == Region([Run(0, 1, 2), (1, 1, 2)])
    assert r != Region([(0, 1, 2)])
    r2 = r.copy()
    r2.runs = [(0, 1, 1)]
    assert r != r2


def test_is_compl_flag():
    r = Region([(1, 1, 2)], is_compl=True)
    assert r.is_compl
    assert bool(r)
    assert r.complement().is_compl is False


def test_bad_run_spec_raises():
    with pytest.raises((ValueError, TypeError)):
        Region([(1, 2)])            # 不是三元组
    with pytest.raises((ValueError, TypeError)):
        Region([1, 2, 3])           # 不是序列


# ---------------------------------------------------------------------------
# 不变量维护
# ---------------------------------------------------------------------------
def test_normalize_sorts_and_merges():
    r = Region([(2, 4, 6), (1, 5, 6), (2, 0, 5)])
    r.normalize()
    assert [(x.r, x.cb, x.ce) for x in r.runs] == [(1, 5, 6), (2, 0, 6)]
    assert r.check_invariants()


def test_bbox_and_area_center():
    r = rect(1, 1, 8, 8)
    assert r.bbox() == (1, 1, 8, 8)
    row, col, area = r.area_center()
    assert area == 64
    assert row == pytest.approx(4.5) and col == pytest.approx(4.5)


def test_is_empty_and_row_ranges():
    r = rect(2, 1, 2, 3)
    assert not r.is_empty(10, 10)
    assert Region([]).is_empty(10, 10)
    begin, end = r.row_ranges(2)
    assert (begin, end) == (0, 1)


def test_materialize_complement():
    r = rect(1, 1, 2, 2)           # 4x4=16 像素，定义域 6x6=36
    comp = r.complement()
    assert comp.is_compl
    mat = comp.materialize(6, 6)
    assert mat.is_compl is False
    assert mat.area_center()[2] == 36 - 4


# ---------------------------------------------------------------------------
# 形状特征（期望值与 cv_region/tests/test_m5_features.cpp 相同）
# ---------------------------------------------------------------------------
def test_features_match_cpp_expectations():
    r = rect(1, 1, 8, 8)

    row, col, area = r.area_center()
    assert area == 64
    assert row == pytest.approx(4.5) and col == pytest.approx(4.5)

    assert r.smallest_rectangle1() == (1, 1, 8, 8)

    ra, rb, _phi = r.elliptic_axis()
    assert abs(ra - rb) < 1.0            # 正方形容器：长短半轴接近

    # contlength = HALCON 'center' 轮廓口径（边界像素中心 8 邻域链，沿轴 1 / 对角 sqrt(2)，
    # 孔洞不计）⇒ 实心 W x H 矩形 = 2*((W-1)+(H-1))：8x8 → 28（旧实现漏整条底边只给 24）。
    # 详见 test_contlength_halcon_center_convention 与根 README。
    assert r.contlength() == 28.0
    assert r.contlength() == pytest.approx(2.0 * (7 + 7))

    conv, is_convex = r.convexity()
    assert is_convex and conv > 0.99

    assert r.rectangularity() > 0.99
    _rt_row, _rt_col, _rt_phi, l1, l2 = r.smallest_rectangle2()
    assert abs(l1 - 4.0) < 1.0 and abs(l2 - 4.0) < 1.0

    assert r.compactness() > 0
    assert r.circularity() > 0
    m11, m20, m02 = r.moments()
    assert m20 > 0 and m02 > 0

    # get_feature 查表与直接调用一致
    assert r.get_feature("area") == pytest.approx(64.0)
    assert r.get_feature("row") == pytest.approx(4.5)


def test_feature_functions_module_level():
    r = rect(1, 1, 8, 8)
    assert cvr.cvr_feature_area_center(r)[2] == 64
    assert cvr.cvr_feature_smallest_rectangle1(r) == (1, 1, 8, 8)
    assert cvr.cvr_feature_contlength(r) == 28.0          # HALCON 'center' 轮廓口径
    assert cvr.cvr_get_feature(r, "area") == pytest.approx(64.0)
    # 模块级函数与 Region 方法必须逐值一致
    assert cvr.cvr_feature_contlength(r) == r.contlength()


def test_contlength_halcon_center_convention():
    """contlength = HALCON 的 'center' 轮廓口径（本次修复的回归基线）。

    依据 HALCON Operator Reference：
      * contlength：轮廓点 = 边界像素中心，相邻轮廓点沿坐标轴距离计 1、对角计 sqrt(2)；
        孔洞轮廓不计；空区域为 0；
      * gen_contour_region_xld：『对角相邻的边界像素，'border' 模式用两段长度 1，
        'center' 模式用单条 sqrt(2) 边』⇒ 贴边跟踪（对角的直接走对角），不是绕直角。
    """
    s2 = math.sqrt(2.0)

    # 实心矩形 = 2*((W-1)+(H-1))
    assert rect(0, 0, 0, 0).contlength() == 0.0            # 单像素：轮廓退化为一点
    assert rect(0, 0, 0, 1).contlength() == 2.0
    assert rect(0, 0, 1, 1).contlength() == 4.0
    assert rect(1, 1, 8, 8).contlength() == 28.0
    assert rect(0, 0, 50, 80).contlength() == 260.0        # 51x81，与 HALCON 实测一致
    assert rect(0, 0, 80, 50).contlength() == 260.0        # 转置不变

    # 转置不变性（任何轮廓长度定义都必须满足；旧实现恒为 2*行数+列宽，不满足）
    for (h, w) in [(1, 2), (2, 3), (4, 8), (7, 9), (51, 81)]:
        assert rect(0, 0, h - 1, w - 1).contlength() == rect(0, 0, w - 1, h - 1).contlength()

    # 对角元素走单条 sqrt(2) 边
    assert Region([(0, 0, 0), (1, 1, 1), (2, 2, 2)]).contlength() == pytest.approx(4 * s2)
    assert Region([(0, 1, 1), (1, 0, 2), (2, 1, 1)]).contlength() == pytest.approx(4 * s2)
    assert Region([(0, 0, 4), (1, 2, 4)]).contlength() == pytest.approx(8 + s2)

    # 孔洞轮廓不计（只算外轮廓）
    ring = Region([(0, 0, 4), (1, 0, 0), (1, 4, 4), (2, 0, 0), (2, 4, 4),
                   (3, 0, 0), (3, 4, 4), (4, 0, 4)])
    assert ring.contlength() == 16.0
    annulus = Region([(0, 0, 7), (1, 0, 7), (2, 0, 1), (2, 6, 7), (3, 0, 1), (3, 6, 7),
                      (4, 0, 1), (4, 6, 7), (5, 0, 1), (5, 6, 7), (6, 0, 7), (7, 0, 7)])
    assert annulus.contlength() == 28.0

    # 空区域 = 0（HALCON 文档明写）
    assert Region([]).contlength() == 0.0

    # 与旧实现（2*行数 + 列宽 = 2H + W）不同
    assert rect(0, 0, 50, 80).contlength() != 183.0


def test_unknown_feature_raises_runtime_error():
    r = rect(0, 0, 1, 1)
    with pytest.raises(RuntimeError):
        r.get_feature("no_such_feature")
    with pytest.raises(RuntimeError):
        cvr.cvr_get_feature(r, "no_such_feature")


# ---------------------------------------------------------------------------
# 集合运算
# ---------------------------------------------------------------------------
def test_set_operations():
    a = rect(0, 0, 3, 3)      # 16
    b = rect(2, 2, 5, 5)      # 16，交集 2x2 = 4
    u = cvr.cvr_union2(a, b, 6, 6)
    assert u.area_center()[2] == 28
    assert u == a.union2(b, 6, 6)

    assert cvr.cvr_intersection(a, b, 6, 6).area_center()[2] == 4
    assert cvr.cvr_difference(a, b, 6, 6).area_center()[2] == 12
    assert cvr.cvr_symm_difference(a, b, 6, 6).area_center()[2] == 24
    assert cvr.cvr_union1([a, b], 6, 6) == u

    # region 为空 → 并集等于另一个
    assert cvr.cvr_union2(Region([]), a, 6, 6) == a
    # 交集为空
    assert cvr.cvr_intersection(rect(0, 0, 1, 1), rect(4, 4, 5, 5), 6, 6) == Region([])


# ---------------------------------------------------------------------------
# 连通域
# ---------------------------------------------------------------------------
def test_connection_simple():
    reg = Region([(1, 1, 2), (2, 1, 2), (1, 5, 6), (2, 5, 6)])
    comps = reg.connection(8, 10, 10)
    assert len(comps) == 2
    assert sorted(c.area_center()[2] for c in comps) == [4, 4]
    # 模块级函数同结果
    assert cvr.cvr_connection(reg, 8, 10, 10) == comps


def test_connection_4_vs_8():
    diag = Region([(0, 0, 0), (1, 1, 1)])     # 对角相邻
    assert len(diag.connection(4, 4, 4)) == 2
    assert len(diag.connection(8, 4, 4)) == 1


# ---------------------------------------------------------------------------
# 形态学
# ---------------------------------------------------------------------------
def test_dilation_erosion_roundtrip():
    sq = rect(4, 4, 6, 6)                 # 3x3 = 9
    se = cvr.cvr_se_rect(3, 3)
    d = sq.dilation1(se, 1, 12, 12)       # 3x3 (+) 3x3 = 5x5
    assert d.area_center()[2] == 25
    e = d.erosion1(se, 1, 12, 12)
    assert e.area_center()[2] == 9

    # 模块级 + 结构元转置
    assert cvr.cvr_dilation1(sq, se, 1, 12, 12) == d
    assert cvr.cvr_se_transpose(cvr.cvr_se_rect(1, 3)).area_center()[2] == 3

    # 开运算对已经是"整块"的矩形是恒等
    assert sq.opening(se, 12, 12).area_center()[2] == 9
    assert sq.closing(se, 12, 12).area_center()[2] == 9


def test_fill_up():
    hollow = Region([
        (1, 1, 5),
        (2, 1, 1), (2, 5, 5),
        (3, 1, 1), (3, 5, 5),
        (4, 1, 1), (4, 5, 5),
        (5, 1, 5),
    ])
    assert hollow.area_center()[2] == 16
    assert hollow.fill_up(8, 8).area_center()[2] == 25


def test_generate_ops():
    assert cvr.cvr_gen_rectangle1(1, 1, 3, 3).area_center()[2] == 9
    assert cvr.cvr_gen_circle(5, 5, 2.0).area_center()[2] > 0
    assert cvr.cvr_gen_rectangle2(5.0, 5.0, 0.0, 2.0, 1.0).area_center()[2] > 0
    assert cvr.cvr_se_disk(2).area_center()[2] > 0
    # 十字结构元 = 竖臂 (2r+1) + 横臂 (2r) = 半径 2 → 5 + 4 = 9
    assert cvr.cvr_se_cross(2).area_center()[2] == 9
    # 3x3 矩形结构元以 (0,0) 为心 → 行 -1..1、列 -1..1
    assert cvr.cvr_se_rect(3, 3).area_center()[2] == 9


def test_shape_trans():
    r = rect(1, 1, 4, 4)                    # 4x4 = 16
    assert r.shape_trans("convex").area_center()[2] == 16
    assert r.shape_trans("rectangle1").area_center()[2] == 16
    assert r.shape_trans("rectangle2").area_center()[2] == 16
    assert r.shape_trans("ellipse").area_center()[2] > 0
    assert cvr.cvr_shape_trans(r, "outer_circle").area_center()[2] > 0
    with pytest.raises(RuntimeError):
        r.shape_trans("no_such_shape")


@pytest.mark.parametrize("n", [3, 4, 5, 8])
def test_shape_trans_convex_contains_input(n):
    """凸包必须包含原区域（定义性质；旧实现丢底部一行，N x N → N*(N-1)）"""
    r = rect(1, 1, n, n)
    conv = r.shape_trans("convex")
    assert conv.area_center()[2] == n * n                     # 凸区域 → 恒等
    assert not cvr.cvr_difference(r, conv, n + 2, n + 2).runs  # 一个原像素都不丢
    assert r.shape_trans("rectangle2").area_center()[2] == n * n
    assert r.shape_trans("convex").bbox() == (1, 1, n, n)


# ---------------------------------------------------------------------------
# select_shape（期望值与 cv_region/tests/test_m7_select.cpp 相同）
# ---------------------------------------------------------------------------
def test_select_shape():
    regs = [rect(1, 1, 3, 3), rect(1, 1, 5, 5), rect(1, 1, 7, 7)]   # 9 / 25 / 49

    # 注：C++ 单测 test_m7_select.cpp 这里写的是 `assert(sel.size() == 1)`，
    #     但 20..50 同时包含 25 与 49 —— 那条断言在 Release 下被编译掉、从未真正
    #     执行过；下面按实际语义（区间闭区间 [min, max]）断言。
    sel = cvr.cvr_select_shape_single(regs, "area", 20.0, 50.0)
    assert len(sel) == 2
    assert [x.area_center()[2] for x in sel] == [25, 49]

    sel = cvr.cvr_select_shape(regs, ["area"], "and", [10.0], [30.0])
    assert len(sel) == 1 and sel[0].area_center()[2] == 25

    sel = cvr.cvr_select_shape(regs, ["area"], "or", [40.0], [100.0])
    assert len(sel) == 1 and sel[0].area_center()[2] == 49

    # 多特征 + and
    sel = cvr.cvr_select_shape(regs, ["area", "width"], "and",
                               [20.0, 5.0], [60.0, 6.0])
    assert len(sel) == 1 and sel[0].area_center()[2] == 25

    # 空特征列表 → 全选
    assert len(cvr.cvr_select_shape(regs, [], "and", [], [])) == 3
    # 未知特征 → 抛异常
    with pytest.raises(RuntimeError):
        cvr.cvr_select_shape(regs, ["no_such_feature"], "and", [0.0], [1.0])
    # mins/maxs 长度不匹配 → 抛异常
    with pytest.raises(RuntimeError):
        cvr.cvr_select_shape(regs, ["area", "width"], "and", [0.0], [1.0, 1.0])


def test_region_features_table():
    regs = [rect(1, 1, 3, 3), rect(1, 1, 5, 5), rect(1, 1, 7, 7)]
    tbl = cvr.cvr_region_features(regs, ["area", "row", "column"])
    assert len(tbl) == 3
    assert all(len(row) == 3 for row in tbl)
    assert [row[0] for row in tbl] == [9.0, 25.0, 49.0]
    assert tbl[2][1] == pytest.approx(4.0)
    assert tbl[0][2] == pytest.approx(2.0)


def test_feature_mask_and_precompute():
    assert cvr.cvr_feature_mask_name("area") == cvr.CVR_FC_CENTER_AREA
    assert cvr.cvr_feature_mask_parse(["area", "contlength"]) == (
        cvr.CVR_FC_CENTER_AREA | cvr.CVR_FC_CONT_LENGTH)
    assert cvr.cvr_feature_mask_parse(["all"]) == cvr.CVR_FC_ALL
    assert cvr.cvr_feature_mask_parse(["basic"]) == cvr.CVR_FC_BASIC
    with pytest.raises(RuntimeError):
        cvr.cvr_feature_mask_name("no_such_feature")

    r = rect(1, 1, 3, 3)
    assert r.precompute_features(cvr.CVR_FC_BASIC) > 0
    # 注意：Feature.flags 是 CvrFeatureFlags 的**字段位**（bit0=shape, bit1=is_convex,
    # ... bit15=center_area, bit16=smallest_rectangle1 ...），不是 CVR_FC_* 掩码位。
    # CVR_FC_BASIC = center_area|moments|contlength|rectangle1 → 0x1C100。
    assert r.feature.flags == 0x1C100
    assert r.feature.area == 9              # center_area 组已写入缓存
    assert r.get_feature("area") == pytest.approx(9.0)
    assert cvr.cvr_region_precompute_features(r, cvr.CVR_FC_BASIC) == 0   # 幂等


# ---------------------------------------------------------------------------
# 灰度特征
# ---------------------------------------------------------------------------
def test_gray_feature():
    img = np.arange(20, dtype=np.uint8).reshape(4, 5)      # 0..19
    full = Region.from_image(np.ones((4, 5), np.uint8), 0.5)
    assert full.area_center()[2] == 20

    assert full.gray_feature(img, 5, 4, "uint8", "mean") == pytest.approx(float(img.mean()))
    assert full.gray_feature(img, 5, 4, "uint8", "min") == pytest.approx(0.0)
    assert full.gray_feature(img, 5, 4, "uint8", "max") == pytest.approx(19.0)
    assert full.gray_feature(img, 5, 4, "uint8", "area") == pytest.approx(float(img.sum()))

    row = cvr.cvr_gray_features([full], img, 5, 4, "uint8", ["mean", "max"])
    assert row == [[pytest.approx(float(img.mean())), pytest.approx(19.0)]]


def test_gray_feature_region_subset():
    img = np.zeros((5, 5), np.float32)
    img[1:3, 1:3] = 9.0                     # 背景 0 < 阈值 0.5 < 前景 9
    r = Region.from_image(img, 0.5)
    assert r.area_center()[2] == 4
    assert r.gray_feature(img, 5, 5, "float32", "mean") == pytest.approx(9.0)


# ---------------------------------------------------------------------------
# region <-> 图像 / mask
# ---------------------------------------------------------------------------
def test_mask_roundtrip_numpy():
    rng = np.random.default_rng(0)
    mask = (rng.random((7, 9)) > 0.5).astype(np.uint8)
    r = Region.from_mask(mask)
    back = r.to_mask(9, 7)
    assert back.dtype == np.uint8
    assert np.array_equal(back > 0, mask > 0)
    assert set(np.unique(back)).issubset({0, 255})

    # 0/255 与 bool 掩码等价
    assert Region.from_mask(mask * 255).to_mask(9, 7).sum() == back.sum()
    assert Region.from_mask(mask.astype(bool)).to_mask(9, 7).sum() == back.sum()


def test_from_mask_buffer_protocol():
    """不依赖 numpy 的路径：2 维 memoryview（buffer 协议）"""
    raw = bytearray(3 * 4)
    raw[1 * 4 + 2] = 1
    mv = memoryview(raw).cast("B", (3, 4))
    r = Region.from_mask(mv)
    assert r.area_center()[2] == 1
    assert r.bbox() == (1, 2, 1, 2)


def test_bin_to_region_and_back():
    img = np.zeros((3, 4), np.uint8)
    img[0, :] = 200
    r = cvr.cvr_bin_to_region(img, 4, 3, "uint8", 128)
    assert [(x.r, x.cb, x.ce) for x in r.runs] == [(0, 0, 3)]

    buf = np.zeros((3, 4), np.uint8)
    cvr.cvr_region_to_bin(r, 4, 3, buf, "uint8", 255, 0)
    assert buf[0].tolist() == [255] * 4
    assert buf[1].tolist() == [0] * 4

    # 便捷：无需 numpy
    raw = r.to_bin_bytes(4, 3)
    assert raw[0] == 255 and raw[4] == 0


def test_from_image_threshold_and_dtypes():
    img = np.zeros((5, 6), np.float32)
    img[1:3, 2:5] = 0.7
    r = Region.from_image(img, 0.5)
    assert r.area_center()[2] == 6
    assert not Region.from_image(img, 0.8)

    # dtype 显式给定时不需要 numpy 推断
    img8 = (img * 255).astype(np.uint8)
    assert Region.from_image(img8, 128, "uint8") == r

    # 深度三写法等价
    assert Region.from_image(img8, 128, GrayDepth.U8) == r
    assert Region.from_image(img8, 128, cvr.CVR_GRAY_U8) == r


def test_to_image_dtypes():
    r = rect(1, 1, 2, 2)
    a = r.to_image(4, 4, "float32", 1.0, 0.0)
    assert a.dtype == np.float32 and a.shape == (4, 4)
    assert a.sum() == pytest.approx(4.0)
    assert a[1:3, 1:3].sum() == pytest.approx(4.0)

    b = r.to_image(4, 4, "uint16", 7, 0)
    assert b.dtype == np.uint16 and b[1, 1] == 7 and b[0, 0] == 0
    assert r.to_mask(4, 4)[1, 1] == 255


# ---------------------------------------------------------------------------
# 错误路径
# ---------------------------------------------------------------------------
def test_ambiguous_depth_name_rejected():
    img = np.zeros((2, 2), np.uint8)
    with pytest.raises(ValueError):
        cvr.cvr_bin_to_region(img, 2, 2, "int8", 1.0)     # 有歧义，明确拒绝
    with pytest.raises(ValueError):
        cvr.cvr_bin_to_region(img, 2, 2, "no_such_depth", 1.0)


def test_itemsize_mismatch_rejected():
    img = np.zeros((2, 2), np.uint8)
    with pytest.raises(ValueError):
        cvr.cvr_bin_to_region(img, 2, 2, "float32", 1.0)


def test_size_mismatch_rejected():
    img = np.zeros((2, 2), np.uint8)
    with pytest.raises(ValueError):
        cvr.cvr_bin_to_region(img, 3, 2, "uint8", 1.0)


def test_non_contiguous_rejected():
    img = np.zeros((6, 6), np.uint8)[::2, ::2]           # 非连续视图
    with pytest.raises(ValueError):
        cvr.cvr_bin_to_region(img, 3, 3, "uint8", 1.0)


def test_readonly_output_rejected():
    r = rect(0, 0, 1, 1)
    with pytest.raises((BufferError, ValueError)):
        cvr.cvr_region_to_bin(r, 2, 2, b"\x00\x00\x00\x00", "uint8", 255, 0)


def test_unsupported_numpy_dtype_rejected():
    with pytest.raises(ValueError):
        Region.from_image(np.zeros((2, 2), np.float16), 0.5)


def test_last_error_roundtrip():
    cvr.cvr_clear_last_error()
    cvr.cvr_set_last_error("手写错误")
    assert cvr.cvr_last_error() == "手写错误"
    cvr.cvr_clear_last_error()
    assert cvr.cvr_last_error() == ""


# ---------------------------------------------------------------------------
# 与 HALCON 语义的口径对照（自检用）
# ---------------------------------------------------------------------------
def test_circle_area_close_to_analytic():
    r = cvr.cvr_gen_circle(50, 50, 20.0)
    area = r.area_center()[2]
    assert area == pytest.approx(math.pi * 20.0 * 20.0, rel=0.05)
