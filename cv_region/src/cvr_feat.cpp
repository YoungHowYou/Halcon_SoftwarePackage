#include "cvr/cvr_feat.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stack>

namespace cvr {

// ----------------------------------------------------------------------------
// 内部工具
// ----------------------------------------------------------------------------

static double cross(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static double dist2(double r1, double c1, double r2, double c2) {
    double dr = r1 - r2, dc = c1 - c2;
    return dr * dr + dc * dc;
}

// 提取边界像素（8-连通边界，每个 run 的端点）
static void extract_boundary_points(const CvrRegion& r,
                                    std::vector<std::pair<double, double>>& pts)
{
    pts.clear();
    pts.reserve(r.runs.size() * 2);
    for (const auto& rr : r.runs) {
        pts.push_back({(double)rr.r, (double)rr.cb});
        pts.push_back({(double)rr.r, (double)rr.ce});
    }
    // 去重
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
}

// 凸包（monotone chain，按列主序）
static void convex_hull(std::vector<std::pair<double, double>>& pts,
                        std::vector<std::pair<double, double>>& hull)
{
    hull.clear();
    if (pts.size() <= 1) { hull = pts; return; }

    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first;
    });

    std::vector<std::pair<double, double>> lower, upper;
    for (const auto& p : pts) {
        while (lower.size() >= 2) {
            auto& q = lower[lower.size() - 1];
            auto& r = lower[lower.size() - 2];
            double cr = cross(q.second - r.second, q.first - r.first,
                              p.second - q.second, p.first - q.first);
            if (cr <= 0) lower.pop_back();
            else break;
        }
        lower.push_back(p);
    }
    for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
        const auto& p = *it;
        while (upper.size() >= 2) {
            auto& q = upper[upper.size() - 1];
            auto& r = upper[upper.size() - 2];
            double cr = cross(q.second - r.second, q.first - r.first,
                              p.second - q.second, p.first - q.first);
            if (cr <= 0) upper.pop_back();
            else break;
        }
        upper.push_back(p);
    }

    lower.pop_back();
    upper.pop_back();
    hull = lower;
    hull.insert(hull.end(), upper.begin(), upper.end());
}

// 多边形面积（Shoelace）
static double polygon_area(const std::vector<std::pair<double, double>>& poly)
{
    if (poly.size() < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        a += poly[i].second * poly[j].first - poly[j].second * poly[i].first;
    }
    return std::abs(a) * 0.5;
}

// ----------------------------------------------------------------------------
bool cvr_feature_area_center(const CvrRegion& r,
                             double& row, double& col, CvrChords& area)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_area_center: empty region");
        return false;
    }

    area = 0;
    double sum_r = 0.0, sum_c = 0.0;
    for (const auto& rr : r.runs) {
        CvrChords len = rr.ce - rr.cb + 1;
        area += len;
        sum_r += (double)rr.r * len;
        sum_c += 0.5 * (rr.cb + rr.ce) * len;
    }
    if (area == 0) {
        row = col = 0.0;
        return true;
    }
    row = sum_r / (double)area;
    col = sum_c / (double)area;
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_feature_moments(const CvrRegion& r,
                         double& m11, double& m20, double& m02)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_moments: empty region");
        return false;
    }

    double row, col;
    CvrChords area;
    if (!cvr_feature_area_center(r, row, col, area)) return false;

    m11 = m20 = m02 = 0.0;
    for (const auto& rr : r.runs) {
        CvrChords len = rr.ce - rr.cb + 1;
        double dr = rr.r - row;

        // sum_{c=cb}^{ce} (c - col)
        double sum_c = 0.5 * (rr.cb + rr.ce) * len;
        double sum_dc = sum_c - col * len;
        m11 += dr * sum_dc;

        // sum_{c=cb}^{ce} (c - col)^2 = sum_c2 - 2*col*sum_c + len*col^2
        double sum_c2 = 0.0;
        for (CvrCoord c = rr.cb; c <= rr.ce; ++c) sum_c2 += (double)c * c;
        m20 += sum_c2 - 2.0 * col * sum_c + len * col * col;

        m02 += dr * dr * len;
    }
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_feature_elliptic_axis(const CvrRegion& r,
                               double& ra, double& rb, double& phi)
{
    cvr_clear_last_error();
    double m11, m20, m02;
    if (!cvr_feature_moments(r, m11, m20, m02)) return false;

    double row, col;
    CvrChords area;
    if (!cvr_feature_area_center(r, row, col, area) || area == 0) {
        ra = rb = phi = 0.0;
        return true;
    }

    double n20 = m20 / (double)area;
    double n02 = m02 / (double)area;
    double n11 = m11 / (double)area;

    // 协方差矩阵 [[n20, n11],[n11, n02]] 的特征值
    double trace = n20 + n02;
    double diff = n20 - n02;
    double det = diff * diff + 4.0 * n11 * n11;
    double sqrt_det = std::sqrt(det);
    double lambda1 = 0.5 * (trace + sqrt_det);
    double lambda2 = 0.5 * (trace - sqrt_det);

    // Halcon: ra = 2*sqrt(lambda_max), rb = 2*sqrt(lambda_min)
    ra = 2.0 * std::sqrt(std::max(0.0, lambda1));
    rb = 2.0 * std::sqrt(std::max(0.0, lambda2));

    // phi: 主轴方向
    phi = 0.5 * std::atan2(2.0 * n11, n20 - n02);
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_feature_contlength(const CvrRegion& r, double& contlength)
{
    cvr_clear_last_error();
    contlength = 0.0;

    // 按行索引 runs
    std::map<CvrCoord, std::vector<CvrRun>> rows;
    for (const auto& rr : r.runs) rows[rr.r].push_back(rr);

    auto find_overlap_len = [&](const CvrRun& a, const CvrRun& b) -> int {
        CvrCoord cb = std::max(a.cb, b.cb);
        CvrCoord ce = std::min(a.ce, b.ce);
        return cb <= ce ? (ce - cb + 1) : 0;
    };

    // 上下行间重叠：决定垂直/对角边贡献
    for (auto& kv : rows) {
        CvrCoord row = kv.first;
        auto& line = kv.second;
        for (const auto& rr : line) {
            // 左侧边界和右侧边界各贡献 1
            contlength += 2.0;

            // 与上一行的重叠
            auto itp = rows.find(row - 1);
            if (itp != rows.end()) {
                int over = 0;
                for (const auto& pr : itp->second) {
                    over += find_overlap_len(rr, pr);
                }
                int len = rr.ce - rr.cb + 1;
                int non_over = len - over;
                if (non_over > 0) {
                    // 非重叠部分与上一行边界形成对角/垂直边
                    contlength += non_over * (std::sqrt(2.0) - 1.0);
                }
            } else {
                // 上一行无前景：整段上边界都是垂直边
                contlength += (rr.ce - rr.cb + 1);
            }

            // 与下一行的重叠（已在上一行处理过，避免重复）
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_feature_convexity(const CvrRegion& r, double& convexity, bool& is_convex)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        convexity = 0.0;
        is_convex = false;
        return true;
    }

    std::vector<std::pair<double, double>> pts, hull;
    extract_boundary_points(r, pts);
    convex_hull(pts, hull);

    double hull_a = polygon_area(hull);
    double row, col;
    CvrChords area;
    if (!cvr_feature_area_center(r, row, col, area)) return false;

    if (hull_a <= 0.0) {
        convexity = 1.0;
        is_convex = true;
        return true;
    }

    convexity = (double)area / hull_a;
    if (convexity > 1.0) convexity = 1.0;
    is_convex = convexity > 0.995;
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_feature_smallest_rectangle1(const CvrRegion& r,
                                     CvrCoord& row1, CvrCoord& col1,
                                     CvrCoord& row2, CvrCoord& col2)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_smallest_rectangle1: empty region");
        return false;
    }
    return cvr_region_bbox(r, row1, col1, row2, col2);
}

// ----------------------------------------------------------------------------
// 旋转卡壳求最小面积外接矩形
bool cvr_feature_smallest_rectangle2(const CvrRegion& r,
                                     double& row, double& col,
                                     double& phi, double& length1, double& length2)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_smallest_rectangle2: empty region");
        return false;
    }

    std::vector<std::pair<double, double>> pts, hull;
    extract_boundary_points(r, pts);
    convex_hull(pts, hull);
    if (hull.size() == 1) {
        row = hull[0].first; col = hull[0].second;
        phi = 0.0; length1 = length2 = 0.0;
        return true;
    }
    if (hull.size() == 2) {
        row = 0.5 * (hull[0].first + hull[1].first);
        col = 0.5 * (hull[0].second + hull[1].second);
        phi = std::atan2(hull[1].first - hull[0].first,
                         hull[1].second - hull[0].second);
        length1 = std::sqrt(dist2(hull[0].first, hull[0].second,
                                  hull[1].first, hull[1].second)) * 0.5;
        length2 = 0.0;
        return true;
    }

    size_t n = hull.size();
    double min_area = 1e300;
    double best_phi = 0.0, best_l1 = 0.0, best_l2 = 0.0;
    double best_r = 0.0, best_c = 0.0;

    // 找到每条边的最远点（卡壳）
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        double dr = hull[j].first - hull[i].first;
        double dc = hull[j].second - hull[i].second;
        double edge_len = std::sqrt(dr * dr + dc * dc);
        if (edge_len < 1e-9) continue;

        // 边方向单位向量（沿边）
        double ux = dc / edge_len;
        double uy = dr / edge_len;
        // 法向（朝外）
        double nx = -uy;
        double ny = ux;

        double min_u = 0.0, max_u = 0.0, min_n = 0.0, max_n = 0.0;
        // 以 hull[i] 为原点投影所有凸包点
        for (size_t k = 0; k < n; ++k) {
            double pr = hull[k].first - hull[i].first;
            double pc = hull[k].second - hull[i].second;
            double u = pc * ux + pr * uy;
            double nv = pc * nx + pr * ny;
            if (k == 0) {
                min_u = max_u = u;
                min_n = max_n = nv;
            } else {
                min_u = std::min(min_u, u);
                max_u = std::max(max_u, u);
                min_n = std::min(min_n, nv);
                max_n = std::max(max_n, nv);
            }
        }

        double w = max_u - min_u;
        double h = max_n - min_n;
        double area = w * h;
        if (area < min_area) {
            min_area = area;
            best_phi = std::atan2(uy, ux);
            best_l1 = 0.5 * std::max(w, h);
            best_l2 = 0.5 * std::min(w, h);
            // 矩形中心 = 原点 + 中点偏移
            double cu = 0.5 * (min_u + max_u);
            double cn = 0.5 * (min_n + max_n);
            best_c = hull[i].second + cu * ux + cn * nx;
            best_r = hull[i].first + cu * uy + cn * ny;
        }
    }

    row = best_r; col = best_c; phi = best_phi;
    length1 = best_l1; length2 = best_l2;
    return true;
}

// ----------------------------------------------------------------------------
// 最小外接圆：基于凸包，用 Welzl 的简化确定性版本
static bool smallest_circle_impl(const std::vector<std::pair<double, double>>& pts,
                                 double& row, double& col, double& radius)
{
    if (pts.empty()) { row = col = radius = 0.0; return true; }
    if (pts.size() == 1) { row = pts[0].first; col = pts[0].second; radius = 0.0; return true; }

    // 先用凸包顶点做暴力搜索 + 局部优化
    std::vector<std::pair<double, double>> hull;
    convex_hull(const_cast<std::vector<std::pair<double, double>>&>(pts), hull);

    double cx = 0.0, cy = 0.0, r2 = 0.0;
    // 初始：包围所有点的圆
    for (const auto& p : hull) { cx += p.first; cy += p.second; }
    cx /= hull.size(); cy /= hull.size();
    for (const auto& p : hull) r2 = std::max(r2, dist2(p.first, p.second, cx, cy));

    // 迭代移动到最远点
    for (int iter = 0; iter < 100; ++iter) {
        double far_r2 = 0.0;
        size_t far_i = 0;
        for (size_t i = 0; i < hull.size(); ++i) {
            double d2 = dist2(hull[i].first, hull[i].second, cx, cy);
            if (d2 > far_r2) { far_r2 = d2; far_i = i; }
        }
        double far_r = std::sqrt(far_r2);
        double cur_r = std::sqrt(r2);
        if (far_r < cur_r + 1e-6 || far_r < 1e-9) break;
        // 向最远点方向微移中心并扩大半径
        double step = (far_r - cur_r) * 0.3;
        cx += step * (hull[far_i].first - cx) / far_r;
        cy += step * (hull[far_i].second - cy) / far_r;
        r2 = 0.0;
        for (const auto& p : hull) r2 = std::max(r2, dist2(p.first, p.second, cx, cy));
    }

    row = cx; col = cy; radius = std::sqrt(r2);
    return true;
}

bool cvr_feature_smallest_circle(const CvrRegion& r,
                                 double& row, double& col, double& radius)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_smallest_circle: empty region");
        return false;
    }
    std::vector<std::pair<double, double>> pts;
    extract_boundary_points(r, pts);
    return smallest_circle_impl(pts, row, col, radius);
}

// ----------------------------------------------------------------------------
bool cvr_feature_compactness(const CvrRegion& r, double& compactness)
{
    cvr_clear_last_error();
    double cl;
    CvrChords area;
    double row, col;
    if (!cvr_feature_contlength(r, cl)) return false;
    if (!cvr_feature_area_center(r, row, col, area)) return false;
    if (area == 0) { compactness = 0.0; return true; }
    compactness = (cl * cl) / (4.0 * CVR_PI * (double)area);
    return true;
}

bool cvr_feature_circularity(const CvrRegion& r, double& circularity)
{
    cvr_clear_last_error();
    double conv;
    bool isconv;
    if (!cvr_feature_convexity(r, conv, isconv)) return false;
    double cl;
    if (!cvr_feature_contlength(r, cl)) return false;
    CvrChords area;
    double row, col;
    if (!cvr_feature_area_center(r, row, col, area)) return false;
    if (area == 0) { circularity = 0.0; return true; }
    // Halcon circularity = contlength^2 / (4*pi*area) 的倒数归一化形式
    double f = cl * cl / (4.0 * CVR_PI * (double)area);
    circularity = 1.0 / std::max(1.0, f);
    return true;
}

bool cvr_feature_rectangularity(const CvrRegion& r, double& rectangularity)
{
    cvr_clear_last_error();
    double conv;
    bool isconv;
    if (!cvr_feature_convexity(r, conv, isconv)) return false;
    CvrChords area;
    double row, col;
    if (!cvr_feature_area_center(r, row, col, area)) return false;
    if (area == 0) { rectangularity = 0.0; return true; }

    double rrow, rcol, phi, l1, l2;
    if (!cvr_feature_smallest_rectangle2(r, rrow, rcol, phi, l1, l2)) return false;
    double rect_area = (2.0 * l1) * (2.0 * l2);
    if (rect_area <= 0.0) { rectangularity = 0.0; return true; }
    rectangularity = (double)area / rect_area;
    if (rectangularity > 1.0) rectangularity = 1.0;
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_get_feature(const CvrRegion& r, const std::string& name, double& value)
{
    cvr_clear_last_error();

    double row, col, ra, rb, phi, cl, conv, comp, circ, rect;
    double rr, rc, rphi, rl1, rl2;
    double cr, cc, crad;
    CvrChords area;
    CvrCoord r1, c1, r2, c2;
    bool isconv;

    // 小写化
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    if (n == "area") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        value = (double)area;
    } else if (n == "row") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        value = row;
    } else if (n == "column" || n == "col") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        value = col;
    } else if (n == "row1") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)r1;
    } else if (n == "column1" || n == "col1") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)c1;
    } else if (n == "row2") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)r2;
    } else if (n == "column2" || n == "col2") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)c2;
    } else if (n == "width") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)(c2 - c1 + 1);
    } else if (n == "height") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)(r2 - r1 + 1);
    } else if (n == "row_rect") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rr;
    } else if (n == "column_rect" || n == "col_rect") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rc;
    } else if (n == "phi_rect") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rphi;
    } else if (n == "length1") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rl1;
    } else if (n == "length2") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rl2;
    } else if (n == "row_circle") {
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = cr;
    } else if (n == "column_circle" || n == "col_circle") {
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = cc;
    } else if (n == "radius") {
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = crad;
    } else if (n == "phi") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = phi;
    } else if (n == "ra") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = ra;
    } else if (n == "rb") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = rb;
    } else if (n == "contlength") {
        if (!cvr_feature_contlength(r, cl)) return false;
        value = cl;
    } else if (n == "convexity") {
        if (!cvr_feature_convexity(r, conv, isconv)) return false;
        value = conv;
    } else if (n == "compactness") {
        if (!cvr_feature_compactness(r, comp)) return false;
        value = comp;
    } else if (n == "circularity") {
        if (!cvr_feature_circularity(r, circ)) return false;
        value = circ;
    } else if (n == "rectangularity") {
        if (!cvr_feature_rectangularity(r, rect)) return false;
        value = rect;
    } else if (n == "anisometry") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = (rb < 1e-9) ? 0.0 : ra / rb;
    } else if (n == "bulkiness") {
        double conv2;
        if (!cvr_feature_convexity(r, conv2, isconv)) return false;
        value = 1.0 / std::max(conv2, 1e-9);
    } else if (n == "structure_factor") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = (ra * ra) / std::max(rb * rb, 1e-9);
    } else {
        cvr_set_last_error("cvr_get_feature: unknown feature name: " + name);
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_region_features(const std::vector<CvrRegion>& regions,
                         const std::vector<std::string>& names,
                         std::vector<double>& values)
{
    cvr_clear_last_error();
    values.clear();
    if (regions.empty() || names.empty()) return true;

    values.reserve(regions.size() * names.size());
    for (const auto& r : regions) {
        for (const auto& name : names) {
            double v;
            if (!cvr_get_feature(r, name, v)) return false;
            values.push_back(v);
        }
    }
    return true;
}

} // namespace cvr

