/*=============================================================================
 * cv_flow/src/features.cpp — 检测/匹配/配准实现（由 Halcon_OpenCV.cpp 迁入，算法未动）
 *===========================================================================*/
#include "cvflow/features.hpp"

#include <cmath>
#include <opencv2/calib3d.hpp>

namespace cvflow {

bool orb_detect(const cv::Mat& img, const OrbParams& p,
                std::vector<cv::KeyPoint>& keypoints, cv::Mat& descU8)
{
    try {
        cv::Ptr<cv::ORB> orb = cv::ORB::create(
            p.nFeatures, (float)p.scaleFactor, p.nlevels, p.edgeThreshold,
            p.firstLevel, p.WTA_K, (cv::ORB::ScoreType)p.scoreType, p.patchSize,
            p.fastThreshold);
        keypoints.clear();
        cv::Mat descriptors;
        orb->detectAndCompute(img, cv::noArray(), keypoints, descriptors);
        descU8 = descriptors;   // ORB 描述子本就是 CV_8U
        return true;
    } catch (const cv::Exception&) {
        keypoints.clear(); descU8.release();
        return false;
    }
}

bool akaze_detect(const cv::Mat& img, const AkazeParams& p,
                  std::vector<cv::KeyPoint>& keypoints, cv::Mat& descU8)
{
    try {
        cv::Ptr<cv::AKAZE> akaze = cv::AKAZE::create(
            (cv::AKAZE::DescriptorType)p.descriptorType, p.descriptorSize,
            p.descriptorChannels, (float)p.threshold, p.nOctaves, p.nOctaveLayers,
            (cv::KAZE::DiffusivityType)p.diffusivity);
        keypoints.clear();
        cv::Mat descriptors;
        akaze->detectAndCompute(img, cv::noArray(), keypoints, descriptors);
        descU8.release();
        if (!descriptors.empty()) {
            if (descriptors.type() != CV_8UC1)
                descriptors.convertTo(descU8, CV_8U);
            else
                descU8 = descriptors;
        }
        return true;
    } catch (const cv::Exception&) {
        keypoints.clear(); descU8.release();
        return false;
    }
}

bool sift_detect(const cv::Mat& img, const SiftParams& p,
                 std::vector<cv::KeyPoint>& keypoints, cv::Mat& descU8)
{
    try {
        // descriptorType 取 CV_8U，使描述子与 bf_knn_match（NORM_HAMMING）直接兼容
        cv::Ptr<cv::SIFT> sift = cv::SIFT::create(
            p.nFeatures, p.nOctaveLayers, p.contrastThreshold, p.edgeThreshold,
            p.sigma, CV_8U);
        keypoints.clear();
        cv::Mat descriptors;
        sift->detectAndCompute(img, cv::noArray(), keypoints, descriptors);
        descU8.release();
        if (!descriptors.empty()) {
            if (descriptors.type() != CV_8UC1)
                descriptors.convertTo(descU8, CV_8U);
            else
                descU8 = descriptors;
        }
        return true;
    } catch (const cv::Exception&) {
        keypoints.clear(); descU8.release();
        return false;
    }
}

void bf_knn_match(const cv::Mat& descRef, const cv::Mat& descTarget,
                  const BfMatchParams& p,
                  std::vector<int>& idxRef, std::vector<int>& idxTarget)
{
    idxRef.clear(); idxTarget.clear();

    const int nRef    = descRef.rows;
    const int nTarget = descTarget.rows;
    if (nRef < 2 || nTarget < 2) return;

    cv::BFMatcher bf(p.normType, p.crossCheck != 0);

    if (p.crossCheck)
    {
        /* crossCheck 模式：必须用 match()（crossCheck 仅在 match/k=1 语义下生效） */
        std::vector<cv::DMatch> ccMatches;
        try { bf.match(descRef, descTarget, ccMatches); }
        catch (const cv::Exception&) { return; }
        for (size_t i = 0; i < ccMatches.size(); i++)
        {
            idxRef.push_back(ccMatches[i].queryIdx);
            idxTarget.push_back(ccMatches[i].trainIdx);
        }
    }
    else
    {
        std::vector<std::vector<cv::DMatch>> knnMatches;
        try { bf.knnMatch(descRef, descTarget, knnMatches, p.knnK); }
        catch (const cv::Exception&) { return; }
        /* Lowe's Ratio Test（需 knnK >= 2） */
        for (size_t i = 0; i < knnMatches.size(); i++)
        {
            if (knnMatches[i].size() == 2)
            {
                const cv::DMatch& m = knnMatches[i][0];
                const cv::DMatch& n = knnMatches[i][1];
                if (m.distance < p.ratioThresh * n.distance)
                {
                    idxRef.push_back(m.queryIdx);
                    idxTarget.push_back(m.trainIdx);
                }
            }
        }
    }
}

bool estimate_affine_partial2d(const std::vector<double>& srcRow,
                               const std::vector<double>& srcCol,
                               const std::vector<double>& dstRow,
                               const std::vector<double>& dstCol,
                               const AffinePartialParams& p,
                               AffinePartialResult& out)
{
    out = AffinePartialResult();
    const int nPts = (int)srcRow.size();
    if (nPts < 4) return false;

    std::vector<cv::Point2f> srcPts((size_t)nPts), dstPts((size_t)nPts);
    for (int i = 0; i < nPts; i++)
    {
        srcPts[(size_t)i] = cv::Point2f((float)srcCol[(size_t)i], (float)srcRow[(size_t)i]);
        dstPts[(size_t)i] = cv::Point2f((float)dstCol[(size_t)i], (float)dstRow[(size_t)i]);
    }

    cv::Mat inlierMask;
    cv::Mat M = cv::estimateAffinePartial2D(
        srcPts, dstPts, inlierMask, p.method, p.ransacThresh,
        (size_t)p.maxIters, p.confidence, (size_t)p.refineIters);

    if (M.empty()) return false;

    int inlierCount = 0;
    if (!inlierMask.empty())
    {
        for (int i = 0; i < inlierMask.rows; i++)
            if (inlierMask.at<uchar>(i, 0) != 0) inlierCount++;
    }

    const double a00 = M.at<double>(0, 0);
    const double a01 = M.at<double>(0, 1);
    const double a02 = M.at<double>(0, 2);
    const double a10 = M.at<double>(1, 0);
    const double a11 = M.at<double>(1, 1);
    const double a12 = M.at<double>(1, 2);

    out.success = true;
    out.inlierCount = inlierCount;
    out.hom[0] = a11; out.hom[1] = a10; out.hom[2] = a12;
    out.hom[3] = a01; out.hom[4] = a00; out.hom[5] = a02;
    out.translateRow = a12;
    out.translateCol = a02;
    out.angle = std::atan2(a10, a00);
    out.scale = std::sqrt(a00 * a00 + a10 * a10);
    return true;
}

bool estimate_rigid_2d(const std::vector<double>& srcRow,
                       const std::vector<double>& srcCol,
                       const std::vector<double>& dstRow,
                       const std::vector<double>& dstCol,
                       const RigidParams& p,
                       RigidResult& out)
{
    out = RigidResult();
    const int nPts = (int)srcRow.size();
    if (nPts < 2) return false;

    std::vector<cv::Point2f> srcPts((size_t)nPts), dstPts((size_t)nPts);
    for (int i = 0; i < nPts; i++)
    {
        srcPts[(size_t)i] = cv::Point2f((float)srcCol[(size_t)i], (float)srcRow[(size_t)i]);
        dstPts[(size_t)i] = cv::Point2f((float)dstCol[(size_t)i], (float)dstRow[(size_t)i]);
    }

    const double thresh2 = p.ransacThresh * p.ransacThresh;

    // ---- RANSAC：每次随机取 2 点求刚体变换，统计内点 ----
    double bestCos = 1.0, bestSin = 0.0, bestTx = 0.0, bestTy = 0.0;
    int bestInliers = 0;

    cv::RNG rng((uint64)p.seed);
    for (int iter = 0; iter < p.maxIter; ++iter)
    {
        int i1 = rng.uniform(0, nPts);
        int i2 = rng.uniform(0, nPts);
        if (i1 == i2) continue;

        double sx = (double)srcPts[(size_t)i2].x - srcPts[(size_t)i1].x;
        double sy = (double)srcPts[(size_t)i2].y - srcPts[(size_t)i1].y;
        double dx = (double)dstPts[(size_t)i2].x - dstPts[(size_t)i1].x;
        double dy = (double)dstPts[(size_t)i2].y - dstPts[(size_t)i1].y;

        double lenSrc2 = sx * sx + sy * sy;
        double lenDst2 = dx * dx + dy * dy;
        if (lenSrc2 < 1e-12 || lenDst2 < 1e-12) continue;

        // 旋转角：cos = (s·d)/(|s||d|), sin = (s×d)/(|s||d|)
        double dot   = sx * dx + sy * dy;
        double cross = sx * dy - sy * dx;
        double inv   = 1.0 / std::sqrt(lenSrc2 * lenDst2);
        double cosT  = dot * inv;
        double sinT  = cross * inv;

        double tx = (double)dstPts[(size_t)i1].x - (cosT * srcPts[(size_t)i1].x - sinT * srcPts[(size_t)i1].y);
        double ty = (double)dstPts[(size_t)i1].y - (sinT * srcPts[(size_t)i1].x + cosT * srcPts[(size_t)i1].y);

        int inliers = 0;
        for (int i = 0; i < nPts; ++i)
        {
            double px = cosT * srcPts[(size_t)i].x - sinT * srcPts[(size_t)i].y + tx;
            double py = sinT * srcPts[(size_t)i].x + cosT * srcPts[(size_t)i].y + ty;
            double ex = px - dstPts[(size_t)i].x;
            double ey = py - dstPts[(size_t)i].y;
            if (ex * ex + ey * ey <= thresh2) ++inliers;
        }

        if (inliers > bestInliers)
        {
            bestInliers = inliers;
            bestCos = cosT; bestSin = sinT;
            bestTx = tx;    bestTy = ty;
        }
    }

    if (bestInliers < 2) return false;

    // ---- 用内点做最小二乘精化（刚体，scale=1）----
    std::vector<cv::Point2f> inSrc, inDst;
    inSrc.reserve((size_t)bestInliers);
    inDst.reserve((size_t)bestInliers);
    for (int i = 0; i < nPts; ++i)
    {
        double px = bestCos * srcPts[(size_t)i].x - bestSin * srcPts[(size_t)i].y + bestTx;
        double py = bestSin * srcPts[(size_t)i].x + bestCos * srcPts[(size_t)i].y + bestTy;
        double ex = px - dstPts[(size_t)i].x;
        double ey = py - dstPts[(size_t)i].y;
        if (ex * ex + ey * ey <= thresh2)
        {
            inSrc.push_back(srcPts[(size_t)i]);
            inDst.push_back(dstPts[(size_t)i]);
        }
    }

    const int nIn = (int)inSrc.size();
    double cSx = 0.0, cSy = 0.0, cDx = 0.0, cDy = 0.0;
    for (int i = 0; i < nIn; ++i)
    {
        cSx += inSrc[(size_t)i].x; cSy += inSrc[(size_t)i].y;
        cDx += inDst[(size_t)i].x; cDy += inDst[(size_t)i].y;
    }
    cSx /= nIn; cSy /= nIn; cDx /= nIn; cDy /= nIn;

    // H = Σ dst_i' * src_i'^T，刚体旋转角闭式解
    double h00 = 0.0, h01 = 0.0, h10 = 0.0, h11 = 0.0;
    for (int i = 0; i < nIn; ++i)
    {
        double sx = inSrc[(size_t)i].x - cSx;
        double sy = inSrc[(size_t)i].y - cSy;
        double dx = inDst[(size_t)i].x - cDx;
        double dy = inDst[(size_t)i].y - cDy;
        h00 += dx * sx;
        h01 += dx * sy;
        h10 += dy * sx;
        h11 += dy * sy;
    }

    const double angle = std::atan2(h10 - h01, h00 + h11);
    const double cosT  = std::cos(angle);
    const double sinT  = std::sin(angle);
    const double tx = cDx - (cosT * cSx - sinT * cSy);
    const double ty = cDy - (sinT * cSx + cosT * cSy);

    out.success = true;
    out.inlierCount = nIn;
    out.hom[0] = cosT;   // a11
    out.hom[1] = sinT;   // a10
    out.hom[2] = ty;     // a12
    out.hom[3] = -sinT;  // a01
    out.hom[4] = cosT;   // a00
    out.hom[5] = tx;     // a02
    out.translateRow = ty;
    out.translateCol = tx;
    out.angle = angle;
    return true;
}

bool estimate_affine_2d(const double* srcRow, const double* srcCol,
                        const double* dstRow, const double* dstCol, int n,
                        const Affine2DParams& p, Affine2DResult& out)
{
    out = Affine2DResult();
    if (n < 3) return false;

    std::vector<cv::Point2f> from((size_t)n), to((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        from[(size_t)i] = cv::Point2f((float)srcCol[i], (float)srcRow[i]);
        to[(size_t)i]   = cv::Point2f((float)dstCol[i], (float)dstRow[i]);
    }

    cv::Mat inlierMask, M;
    try
    {
        M = cv::estimateAffine2D(from, to, inlierMask,
                                 p.method, p.ransacThresh,
                                 (size_t)p.maxIters, p.confidence,
                                 (size_t)p.refineIters);
    }
    catch (const cv::Exception&)
    {
        M = cv::Mat();
    }

    if (M.empty()) return false;

    const double a00 = M.at<double>(0, 0), a01 = M.at<double>(0, 1), a02 = M.at<double>(0, 2);
    const double a10 = M.at<double>(1, 0), a11 = M.at<double>(1, 1), a12 = M.at<double>(1, 2);
    // HALCON hom_mat2d 顺序 [R00,R10,T0,R01,R11,T1] = [a00,a10,a02,a01,a11,a12]
    out.hom[0] = a00; out.hom[1] = a10; out.hom[2] = a02;
    out.hom[3] = a01; out.hom[4] = a11; out.hom[5] = a12;
    out.success = true;
    if (!inlierMask.empty())
        for (int i = 0; i < inlierMask.rows; ++i)
            if (inlierMask.at<uchar>(i, 0) != 0) ++out.inliers;
    return true;
}

} // namespace cvflow
