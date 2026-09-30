/*=============================================================================
 * cvflow/features.hpp — 特征检测 / 描述子匹配 / 几何配准流程
 * 由扩展包 supply（Halcon_OpenCV.cpp）的流程体原样迁入，算法未动。
 * 依赖 OpenCV（features2d / calib3d）。
 *===========================================================================*/
#pragma once

#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

namespace cvflow {

// ---------- 检测参数（默认值 = 原 supply 的缺省值） ----------
struct OrbParams {
    int    nFeatures     = 3000;
    double scaleFactor   = 1.2;
    int    nlevels       = 8;
    int    edgeThreshold = 31;
    int    firstLevel    = 0;
    int    WTA_K         = 2;
    int    scoreType     = 0;   // 0=HARRIS, 1=FAST
    int    patchSize     = 31;
    int    fastThreshold = 20;
};
struct AkazeParams {
    int    descriptorType     = 4;     // 4=MLDB（默认）；0=KAZE,1=KAZE_UPRIGHT,2=MLDB_UPRIGHT
    int    descriptorSize     = 0;
    int    descriptorChannels = 3;
    double threshold          = 0.001;
    int    nOctaves           = 4;
    int    nOctaveLayers      = 4;
    int    diffusivity        = 1;     // 1=PM_G2；0=PM_G1,2=WEICKERT,3=CHARBONNIER
};
struct SiftParams {
    int    nFeatures         = 0;
    int    nOctaveLayers     = 3;
    double contrastThreshold = 0.04;
    double edgeThreshold     = 10.0;
    double sigma             = 1.6;
};

// 检测并计算描述子。akaze/sift 的描述子统一转 CV_8U（与 NORM_HAMMING 匹配兼容），
// 与原 supply 行为一致；descU8 为空表示无关键点。cv::Exception 时返回 false。
bool orb_detect(const cv::Mat& img, const OrbParams& p,
                std::vector<cv::KeyPoint>& keypoints, cv::Mat& descU8);
bool akaze_detect(const cv::Mat& img, const AkazeParams& p,
                  std::vector<cv::KeyPoint>& keypoints, cv::Mat& descU8);
bool sift_detect(const cv::Mat& img, const SiftParams& p,
                 std::vector<cv::KeyPoint>& keypoints, cv::Mat& descU8);

// ---------- BF 匹配（ratio test 或 crossCheck） ----------
struct BfMatchParams {
    int    descWidth   = 32;
    double ratioThresh = 0.75;
    int    normType    = 4;   // 4=NORM_HAMMING；1=L1,2=L2,6=NORM_HAMMING2
    int    crossCheck  = 0;   // 0=否，1=是（忽略 ratio，返回互为最佳匹配）
    int    knnK        = 2;   // ratio 过滤需要 >= 2
};
// 输入为 CV_8UC1 描述子矩阵（n × descWidth）。返回匹配的 (ref, target) 下标对；
// 失败/无匹配返回空（与原 supply 写 NumGoodMatches=0 语义一致）。
void bf_knn_match(const cv::Mat& descRef, const cv::Mat& descTarget,
                  const BfMatchParams& p,
                  std::vector<int>& idxRef, std::vector<int>& idxTarget);

// ---------- 几何配准 ----------
struct AffinePartialParams {
    double ransacThresh = 3.0;
    int    method       = 8;      // 8=RANSAC, 4=LMEDS
    int    maxIters     = 2000;
    double confidence   = 0.99;
    int    refineIters  = 10;
};
struct AffinePartialResult {
    bool success = false;
    int  inlierCount = 0;
    double hom[6] = {0,0,0,0,0,0};  // [a11,a10,a12,a01,a00,a02]
    double angle = 0, scale = 0, translateRow = 0, translateCol = 0;
};
bool estimate_affine_partial2d(const std::vector<double>& srcRow,
                               const std::vector<double>& srcCol,
                               const std::vector<double>& dstRow,
                               const std::vector<double>& dstCol,
                               const AffinePartialParams& p,
                               AffinePartialResult& out);   // false = 估计失败

struct RigidParams {
    double ransacThresh = 3.0;
    int    maxIter      = 500;
    int    seed         = 12345;
};
struct RigidResult {
    bool success = false;
    int  inlierCount = 0;
    double hom[6] = {0,0,0,0,0,0};
    double angle = 0, translateRow = 0, translateCol = 0;   // scale 恒为 1
};
bool estimate_rigid_2d(const std::vector<double>& srcRow,
                       const std::vector<double>& srcCol,
                       const std::vector<double>& dstRow,
                       const std::vector<double>& dstCol,
                       const RigidParams& p,
                       RigidResult& out);

struct Affine2DParams {
    int    method       = 8;
    double ransacThresh = 3.0;
    int    maxIters     = 2000;
    double confidence   = 0.99;
    int    refineIters  = 10;
};
struct Affine2DResult {
    bool success = false;
    int  inliers = 0;
    double hom[6] = {0,0,0,0,0,0};  // [a00,a10,a02,a01,a11,a12]（HALCON 序）
};
bool estimate_affine_2d(const double* srcRow, const double* srcCol,
                        const double* dstRow, const double* dstCol, int n,
                        const Affine2DParams& p, Affine2DResult& out);

} // namespace cvflow
