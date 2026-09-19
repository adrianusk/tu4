// lab_color.cpp — CIE L*a*b* colour-distance helper.
//
// Ported 1:1 from Playscii's lab_color.py (which notes it is "from EDSCII").
//   Playscii: Copyright (c) 2014-2021 JP LeBreton — MIT License.
//   See THIRD-PARTY-LICENSES for the full MIT text.
//
// This is a faithful line-by-line translation; constants and formulas are
// preserved exactly so the L*a*b* colour-diff table matches Playscii's, which
// is required for byte-identical png->ASP output.

#include "lab_color.h"
#include <cmath>

namespace tu4setup {

void rgb_to_xyz(double r, double g, double b,
                double &x, double &y, double &z) {
    r /= 255.0; g /= 255.0; b /= 255.0;
    if (r > 0.04045) r = std::pow((r + 0.055) / 1.055, 2.4); else r /= 12.92;
    if (g > 0.04045) g = std::pow((g + 0.055) / 1.055, 2.4); else g /= 12.92;
    if (b > 0.04045) b = std::pow((b + 0.055) / 1.055, 2.4); else b /= 12.92;
    r *= 100.0; g *= 100.0; b *= 100.0;
    // observer 2deg, illuminant D65
    x = r * 0.4124 + g * 0.3576 + b * 0.1805;
    y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    z = r * 0.0193 + g * 0.1192 + b * 0.9505;
}

void xyz_to_lab(double x, double y, double z,
                double &l, double &a, double &b) {
    x /= 95.047; y /= 100.0; z /= 108.883;
    if (x > 0.008856) x = std::pow(x, 1.0 / 3.0); else x = (7.787 * x) + (16.0 / 116.0);
    if (y > 0.008856) y = std::pow(y, 1.0 / 3.0); else y = (7.787 * y) + (16.0 / 116.0);
    if (z > 0.008856) z = std::pow(z, 1.0 / 3.0); else z = (7.787 * z) + (16.0 / 116.0);
    l = (116.0 * y) - 16.0;
    a = 500.0 * (x - y);
    b = 200.0 * (y - z);
}

void rgb_to_lab(double r, double g, double b,
                double &l, double &a, double &bb) {
    double x, y, z;
    rgb_to_xyz(r, g, b, x, y, z);
    xyz_to_lab(x, y, z, l, a, bb);
}

double lab_color_diff(double l1, double a1, double b1,
                      double l2, double a2, double b2) {
    double dl = (l1 - l2) * (l1 - l2);
    double da = (a1 - a2) * (a1 - a2);
    double db = (b1 - b2) * (b1 - b2);
    return std::sqrt(dl + da + db);
}

} // namespace tu4setup
