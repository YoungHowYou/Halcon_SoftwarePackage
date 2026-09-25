#include "cvr/cvr_io.hpp"

#ifdef CVR_WITH_OPENCV
#include <opencv2/imgproc.hpp>

namespace cvr {

bool cvr_region_to_mask(const CvrRegion& r, CvrCoord w, CvrCoord h,
                        cv::Mat& mask) {
    cvr_clear_last_error();
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_region_to_mask: invalid domain size");
        return false;
    }

    mask.create(h, w, CV_8UC1);
    mask.setTo(cv::Scalar(0));

    if (r.is_compl) {
        CvrRegion mat;
        if (!cvr_region_materialize(r, w, h, mat)) {
            return false;
        }
        for (const auto& rr : mat.runs) {
            cv::line(mask, {rr.cb, rr.r}, {rr.ce, rr.r}, cv::Scalar(255));
        }
    } else {
        for (const auto& rr : r.runs) {
            if (rr.r < 0 || rr.r >= h) continue;
            CvrCoord cb = std::max<CvrCoord>(0, rr.cb);
            CvrCoord ce = std::min<CvrCoord>(w - 1, rr.ce);
            if (cb <= ce) {
                cv::line(mask, {cb, rr.r}, {ce, rr.r}, cv::Scalar(255));
            }
        }
    }
    return true;
}

bool cvr_region_from_mask(const cv::Mat& mask, CvrRegion& r,
                          CvrCoord* out_w, CvrCoord* out_h) {
    cvr_clear_last_error();
    if (mask.empty()) {
        cvr_set_last_error("cvr_region_from_mask: empty mask");
        return false;
    }
    if (mask.channels() != 1) {
        cvr_set_last_error("cvr_region_from_mask: mask must be single channel");
        return false;
    }

    CvrCoord w = static_cast<CvrCoord>(mask.cols);
    CvrCoord h = static_cast<CvrCoord>(mask.rows);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    r.runs.clear();
    r.is_compl = false;
    cvr_region_invalidate(r);

    cv::Mat tmp;
    if (mask.type() != CV_8UC1) {
        mask.convertTo(tmp, CV_8UC1);
    } else {
        tmp = mask;
    }

    for (CvrCoord row = 0; row < h; ++row) {
        const uchar* p = tmp.ptr<uchar>(row);
        CvrCoord cb = -1;
        for (CvrCoord col = 0; col < w; ++col) {
            if (p[col] != 0) {
                if (cb < 0) cb = col;
            } else if (cb >= 0) {
                r.runs.push_back({row, cb, static_cast<CvrCoord>(col - 1)});
                cb = -1;
            }
        }
        if (cb >= 0) {
            r.runs.push_back({row, cb, static_cast<CvrCoord>(w - 1)});
        }
    }

    return cvr_region_normalize(r);
}

} // namespace cvr

#endif // CVR_WITH_OPENCV
