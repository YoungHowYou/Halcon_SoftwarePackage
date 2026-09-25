#include "cvr/cvr_select.hpp"
#include "cvr/cvr_feat.hpp"
#include <algorithm>
#include <cctype>

namespace cvr {

bool cvr_select_shape(const std::vector<CvrRegion>& regions,
                      const std::vector<std::string>& features,
                      const std::string& op,
                      const std::vector<double>& mins,
                      const std::vector<double>& maxs,
                      std::vector<CvrRegion>& selected)
{
    cvr_clear_last_error();
    selected.clear();

    if (features.size() != mins.size() || features.size() != maxs.size()) {
        cvr_set_last_error("cvr_select_shape: features/mins/maxs size mismatch");
        return false;
    }
    if (features.empty()) {
        selected = regions;
        return true;
    }

    std::string operation = op;
    std::transform(operation.begin(), operation.end(), operation.begin(), ::tolower);
    bool use_and = (operation == "and");
    if (!use_and && operation != "or") {
        cvr_set_last_error("cvr_select_shape: operation must be 'and' or 'or'");
        return false;
    }

    for (const auto& r : regions) {
        bool pass = use_and ? true : false;
        for (size_t i = 0; i < features.size(); ++i) {
            double v;
            if (!cvr_get_feature(r, features[i], v)) return false;
            bool in_range = (v >= mins[i] && v <= maxs[i]);
            if (use_and) {
                pass = pass && in_range;
                if (!pass) break;
            } else {
                pass = pass || in_range;
                if (pass) break;
            }
        }
        if (pass) selected.push_back(r);
    }
    return true;
}

bool cvr_select_shape_single(const std::vector<CvrRegion>& regions,
                             const std::string& feature,
                             double min_val, double max_val,
                             std::vector<CvrRegion>& selected)
{
    return cvr_select_shape(regions, {feature}, "and", {min_val}, {max_val}, selected);
}

} // namespace cvr
