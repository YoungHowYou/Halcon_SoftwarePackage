#include "cvr/cvr_shape.hpp"
#include "cvr/cvr_feat.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

namespace cvr {

static void add_ellipse_runs(CvrRegion& out, double cx, double cy,
                             double ra, double rb, double phi)
{
    // 离散生成椭圆边界并填充
    std::vector<CvrRun> runs;
    int steps = std::max(36, (int)(2.0 * CVR_PI * std::max(ra, rb) / 2.0));
    std::set<std::pair<CvrCoord, CvrCoord>> pts;

    double c = std::cos(phi), s = std::sin(phi);
    for (int i = 0; i < steps; ++i) {
        double t = 2.0 * CVR_PI * i / steps;
        double lx = ra * std::cos(t);
        double ly = rb * std::sin(t);
        double x = cy + c * lx - s * ly; // 列
        double y = cx + s * lx + c * ly; // 行
        CvrCoord r = (CvrCoord)std::round(y);
        CvrCoord ccol = (CvrCoord)std::round(x);
        pts.insert({r, ccol});
    }

    // 扫描线填充：对每一行找最小最大列
    std::map<CvrCoord, std::pair<CvrCoord, CvrCoord>> rows;
    for (auto& p : pts) {
        auto it = rows.find(p.first);
        if (it == rows.end()) rows[p.first] = {p.second, p.second};
        else {
            if (p.second < it->second.first) it->second.first = p.second;
            if (p.second > it->second.second) it->second.second = p.second;
        }
    }
    for (auto& kv : rows) {
        out.runs.push_back({kv.first, kv.second.first, kv.second.second});
    }
}

bool cvr_shape_trans(const CvrRegion& r, const std::string& shape, CvrRegion& out)
{
    cvr_clear_last_error();
    out.runs.clear();
    out.is_compl = false;

    std::string s = shape;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s == "rectangle1") {
        CvrCoord r1, c1, r2, c2;
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        for (CvrCoord rr = r1; rr <= r2; ++rr)
            out.runs.push_back({rr, c1, c2});
    }
    else if (s == "rectangle2") {
        double cx, cy, phi, l1, l2;
        if (!cvr_feature_smallest_rectangle2(r, cx, cy, phi, l1, l2)) return false;
        // 生成旋转矩形：遍历局部坐标 u,v
        double c = std::cos(phi), s = std::sin(phi);
        double w = 2.0 * l1, h = 2.0 * l2;
        // 简化：采样边界点然后扫描线填充
        std::set<std::pair<CvrCoord, CvrCoord>> pts;
        int n = std::max(20, (int)(w + h));
        for (int i = 0; i < n; ++i) {
            double t = 2.0 * CVR_PI * i / n;
            double u = 0.5 * w * std::cos(t);
            double v = 0.5 * h * std::sin(t);
            double x = cy + c * u - s * v;
            double y = cx + s * u + c * v;
            pts.insert({(CvrCoord)std::round(y), (CvrCoord)std::round(x)});
        }
        std::map<CvrCoord, std::pair<CvrCoord, CvrCoord>> rows;
        for (auto& p : pts) {
            auto it = rows.find(p.first);
            if (it == rows.end()) rows[p.first] = {p.second, p.second};
            else {
                if (p.second < it->second.first) it->second.first = p.second;
                if (p.second > it->second.second) it->second.second = p.second;
            }
        }
        for (auto& kv : rows) out.runs.push_back({kv.first, kv.second.first, kv.second.second});
    }
    else if (s == "ellipse") {
        double cx, cy, ra, rb, phi;
        CvrChords area;
        if (!cvr_feature_area_center(r, cx, cy, area)) return false;
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        add_ellipse_runs(out, cx, cy, ra * 0.5, rb * 0.5, phi);
    }
    else if (s == "outer_circle") {
        double cx, cy, rad;
        if (!cvr_feature_smallest_circle(r, cx, cy, rad)) return false;
        add_ellipse_runs(out, cx, cy, rad, rad, 0.0);
    }
    else if (s == "convex") {
        // 用凸包顶点生成填充多边形
        std::vector<std::pair<double, double>> pts, hull;
        for (const auto& rr : r.runs) {
            pts.push_back({(double)rr.r, (double)rr.cb});
            pts.push_back({(double)rr.r, (double)rr.ce});
        }
        // monotone chain
        std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        });
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());

        std::vector<std::pair<double, double>> lower, upper;
        for (const auto& p : pts) {
            while (lower.size() >= 2) {
                auto& q = lower.back();
                auto& rr = lower[lower.size() - 2];
                double cr = (q.second - rr.second) * (p.first - q.first) -
                            (q.first - rr.first) * (p.second - q.second);
                if (cr <= 0) lower.pop_back(); else break;
            }
            lower.push_back(p);
        }
        for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
            const auto& p = *it;
            while (upper.size() >= 2) {
                auto& q = upper.back();
                auto& rr = upper[upper.size() - 2];
                double cr = (q.second - rr.second) * (p.first - q.first) -
                            (q.first - rr.first) * (p.second - q.second);
                if (cr <= 0) upper.pop_back(); else break;
            }
            upper.push_back(p);
        }
        lower.pop_back(); upper.pop_back();
        hull = lower;
        hull.insert(hull.end(), upper.begin(), upper.end());

        // 扫描线填充凸多边形
        if (!hull.empty()) {
            double min_r = hull[0].first, max_r = hull[0].first;
            for (auto& p : hull) { if (p.first < min_r) min_r = p.first; if (p.first > max_r) max_r = p.first; }
            for (CvrCoord row = (CvrCoord)std::floor(min_r); row <= (CvrCoord)std::ceil(max_r); ++row) {
                std::vector<double> xs;
                for (size_t i = 0; i < hull.size(); ++i) {
                    size_t j = (i + 1) % hull.size();
                    double r1 = hull[i].first, r2 = hull[j].first;
                    double c1 = hull[i].second, c2 = hull[j].second;
                    if ((r1 <= row && r2 > row) || (r2 <= row && r1 > row)) {
                        double t = (row - r1) / (r2 - r1);
                        xs.push_back(c1 + t * (c2 - c1));
                    }
                }
                if (xs.size() >= 2) {
                    std::sort(xs.begin(), xs.end());
                    out.runs.push_back({row, (CvrCoord)std::floor(xs.front()), (CvrCoord)std::ceil(xs.back())});
                }
            }
        }
    }
    else {
        cvr_set_last_error("cvr_shape_trans: unknown shape type: " + shape);
        return false;
    }

    return cvr_region_normalize(out);
}

} // namespace cvr

