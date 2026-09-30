/*=============================================================================
 * test_m10_cache.cpp —— 惰性特征缓存 / 掩码预计算 的正确性验证
 *
 * 覆盖：
 *   1) 缓存不改变数值口径：直算 vs 预计算后读缓存，逐个特征 **逐位相同**
 *   2) cvr_region_precompute_features 幂等（第二次返回 0，即不再新置位）
 *   3) cvr_region_invalidate 后重算的值仍逐位相同
 *   4) cvr_feature_mask_name / cvr_feature_mask_parse 名字解析
 *      （组合名 none/basic/cheap/hull/all、'|' 拼接、大小写与空白、未知名报错）
 *   5) 仅预计算部分组时，其余特征仍按需计算且值一致（部分缓存）
 *   6) 补集 region 走同一口径
 *===========================================================================*/
#include "cvr/cvr.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace cvr;

#define CVR_CHECK(expr)                                          \
    do {                                                         \
        bool _ok = (expr);                                       \
        assert(_ok);                                             \
        if (!_ok) {                                              \
            std::cerr << "CHECK failed: " #expr "\n";            \
            return 1;                                            \
        }                                                        \
    } while (0)

/*-----------------------------------------------------------------------------
 * 测试用形状
 *---------------------------------------------------------------------------*/
static CvrRegion rect(CvrCoord r1, CvrCoord c1, CvrCoord r2, CvrCoord c2) {
    CvrRegion r;
    for (CvrCoord rr = r1; rr <= r2; ++rr) r.runs.push_back({rr, c1, c2});
    return r;
}

/* 非凸 L 形：10x10 去掉右上 5x5 */
static CvrRegion lshape() {
    CvrRegion r;
    for (CvrCoord rr = 0; rr < 10; ++rr) {
        if (rr < 5) r.runs.push_back({rr, 0, 9});
        else        r.runs.push_back({rr, 0, 4});
    }
    return r;
}

/* 环（带孔）：10x10 挖掉 (3,3)-(6,6) */
static CvrRegion annulus() {
    CvrRegion r;
    for (CvrCoord rr = 0; rr < 10; ++rr) {
        if (rr >= 3 && rr <= 6) {
            r.runs.push_back({rr, 0, 2});
            r.runs.push_back({rr, 7, 9});
        } else {
            r.runs.push_back({rr, 0, 9});
        }
    }
    return r;
}

/* 细长斜条（run 数多，凸包/最小外接圆代价高） */
static CvrRegion diagonal() {
    CvrRegion r;
    for (CvrCoord rr = 0; rr < 20; ++rr) {
        const CvrCoord c = rr * 2;
        r.runs.push_back({rr, c, c + 3});
    }
    return r;
}

/* 补集 region（is_compl = true） */
static CvrRegion compl_square() {
    CvrRegion r;
    for (CvrCoord rr = 2; rr <= 7; ++rr) r.runs.push_back({rr, 2, 7});
    r.is_compl = true;
    return r;
}

/*-----------------------------------------------------------------------------
 * 取一整套特征值用于比较（顺序固定）
 *---------------------------------------------------------------------------*/
struct FeatureVec {
    double area, row, col;
    double row1, col1, row2, col2;
    double row_rect, col_rect, phi_rect, length1, length2;
    double row_circle, col_circle, radius;
    double m11, m20, m02;
    double ra, rb, phi;
    double contlength, convexity, compactness, circularity, rectangularity;
    double anisometry, bulkiness, structure_factor;
};

static bool same(const FeatureVec& a, const FeatureVec& b) {
    return std::memcmp(&a, &b, sizeof(FeatureVec)) == 0;
}

static bool compute_all(const CvrRegion& r, FeatureVec& v) {
    std::memset(&v, 0, sizeof(v));
    CvrChords area = 0;
    if (!cvr_feature_area_center(r, v.row, v.col, area)) return false;
    v.area = static_cast<double>(area);

    CvrCoord r1, c1, r2, c2;
    if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
    v.row1 = r1; v.col1 = c1; v.row2 = r2; v.col2 = c2;

    if (!cvr_feature_smallest_rectangle2(r, v.row_rect, v.col_rect, v.phi_rect,
                                         v.length1, v.length2)) return false;
    if (!cvr_feature_smallest_circle(r, v.row_circle, v.col_circle, v.radius))
        return false;
    if (!cvr_feature_moments(r, v.m11, v.m20, v.m02)) return false;
    if (!cvr_feature_elliptic_axis(r, v.ra, v.rb, v.phi)) return false;
    if (!cvr_feature_contlength(r, v.contlength)) return false;

    bool isconv = false;
    if (!cvr_feature_convexity(r, v.convexity, isconv)) return false;
    if (!cvr_feature_compactness(r, v.compactness)) return false;
    if (!cvr_feature_circularity(r, v.circularity)) return false;
    if (!cvr_feature_rectangularity(r, v.rectangularity)) return false;

    if (!cvr_get_feature(r, "anisometry", v.anisometry)) return false;
    if (!cvr_get_feature(r, "bulkiness", v.bulkiness)) return false;
    if (!cvr_get_feature(r, "structure_factor", v.structure_factor)) return false;
    return true;
}

int main() {
    const std::vector<CvrRegion> shapes = {
        rect(1, 1, 8, 8),
        rect(0, 0, 0, 0),          /* 单像素 */
        lshape(),
        annulus(),
        diagonal(),
        compl_square(),
    };

    /* ---- 1/2/3：缓存不改变数值、预计算幂等、invalidate 后重算一致 ---- */
    for (size_t i = 0; i < shapes.size(); ++i) {
        CvrRegion fresh = shapes[i];       /* 无缓存 */
        FeatureVec base;
        CVR_CHECK(compute_all(fresh, base));
        CVR_CHECK(fresh.feature.flags.any());   /* 直算也应填满缓存 */

        CvrRegion pre = shapes[i];         /* 预计算 */
        const uint32_t got = cvr_region_precompute_features(pre, CVR_FC_ALL);
        CVR_CHECK(got != 0u);
        /* 幂等：第二次不再新置位 */
        CVR_CHECK(cvr_region_precompute_features(pre, CVR_FC_ALL) == 0u);
        /* mask=0 / none 不做事 */
        CVR_CHECK(cvr_region_precompute_features(pre, CVR_FC_NONE) == 0u);

        FeatureVec cached;
        CVR_CHECK(compute_all(pre, cached));    /* 全部命中缓存 */
        CVR_CHECK(same(base, cached));
        /* 再读一次仍一致（缓存值稳定） */
        FeatureVec again;
        CVR_CHECK(compute_all(pre, again));
        CVR_CHECK(same(base, again));

        /* invalidate 后重算：值与带缓存时逐位相同 */
        CvrRegion inv = shapes[i];
        cvr_region_invalidate(inv);
        CVR_CHECK(!inv.feature.flags.any());
        FeatureVec rst;
        CVR_CHECK(compute_all(inv, rst));
        CVR_CHECK(same(base, rst));
    }

    /* ---- 4：部分缓存：只预计算 area/bbox，其余按需算，值仍一致 ---- */
    {
        CvrRegion a = shapes[2];      /* L 形 */
        CvrRegion b = shapes[2];
        FeatureVec va, vb;
        CVR_CHECK(compute_all(a, va));
        CVR_CHECK(cvr_region_precompute_features(b, CVR_FC_CENTER_AREA | CVR_FC_RECTANGLE1) != 0u);
        CVR_CHECK(b.feature.flags.center_area && b.feature.flags.smallest_rectangle1);
        CVR_CHECK(!b.feature.flags.smallest_rectangle2);   /* 未预计算，仍未置位 */
        CVR_CHECK(compute_all(b, vb));
        CVR_CHECK(same(va, vb));
    }

    /* ---- 5：掩码名字解析 ---- */
    {
        uint32_t m = 123u;
        CVR_CHECK(cvr_feature_mask_parse({"all"}, m) && m == CVR_FC_ALL);
        CVR_CHECK(cvr_feature_mask_parse({"none"}, m) && m == CVR_FC_NONE);
        CVR_CHECK(cvr_feature_mask_parse({"NONE"}, m) && m == CVR_FC_NONE);
        CVR_CHECK(cvr_feature_mask_parse({"basic"}, m) && m == CVR_FC_BASIC);
        CVR_CHECK(cvr_feature_mask_parse({"cheap"}, m) && m == CVR_FC_BASIC);
        CVR_CHECK(cvr_feature_mask_parse({"hull"}, m) && m == CVR_FC_HULL);
        CVR_CHECK(cvr_feature_mask_parse({"basic", "hull"}, m) &&
                  m == (CVR_FC_BASIC | CVR_FC_HULL));
        CVR_CHECK(cvr_feature_mask_parse({"area|bbox"}, m) &&
                  m == (CVR_FC_CENTER_AREA | CVR_FC_RECTANGLE1));
        CVR_CHECK(cvr_feature_mask_parse({" Area ", "COLUMN"}, m) &&
                  m == CVR_FC_CENTER_AREA);
        CVR_CHECK(!cvr_feature_mask_parse({"no_such_feature"}, m));
        CVR_CHECK(m == 0u);            /* 失败时掩码被清空，避免半成品 */

        uint32_t bits = 0;
        CVR_CHECK(cvr_feature_mask_name("circularity", bits) &&
                  bits == CVR_FC_CIRCULARITY);
        CVR_CHECK(cvr_feature_mask_name(" smallest_circle ", bits) &&
                  bits == CVR_FC_CIRCLE);
        CVR_CHECK(cvr_feature_mask_name("radius", bits) && bits == CVR_FC_CIRCLE);
        CVR_CHECK(cvr_feature_mask_name("struct_factor", bits) &&
                  bits == CVR_FC_EXCENTRICITY);
        CVR_CHECK(!cvr_feature_mask_name("", bits));
        CVR_CHECK(!cvr_feature_mask_name("area_center", bits));   /* 只认单特征名 */
    }

    /* ---- 6：预计算的逐位等价（掩码解析 -> 预计算 -> 读值）---- */
    {
        CvrRegion a = shapes[3];      /* 环 */
        CvrRegion b = shapes[3];
        FeatureVec va, vb;
        CVR_CHECK(compute_all(a, va));

        uint32_t mask = 0u;
        CVR_CHECK(cvr_feature_mask_parse({"area|circularity|rectangularity|hull"},
                                         mask));
        CVR_CHECK(cvr_region_precompute_features(b, mask) != 0u);
        CVR_CHECK(compute_all(b, vb));
        CVR_CHECK(same(va, vb));
    }

    std::cout << "test_m10_cache: all checks passed\n";
    return 0;
}
