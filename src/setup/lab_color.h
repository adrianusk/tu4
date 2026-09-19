// lab_color.h — CIE L*a*b* colour-distance helper.
// Ported from Playscii lab_color.py — © 2014-2021 JP LeBreton, MIT License.
// See THIRD-PARTY-LICENSES.
#ifndef TU4SETUP_LAB_COLOR_H
#define TU4SETUP_LAB_COLOR_H

namespace tu4setup {

void rgb_to_xyz(double r, double g, double b, double &x, double &y, double &z);
void xyz_to_lab(double x, double y, double z, double &l, double &a, double &b);
void rgb_to_lab(double r, double g, double b, double &l, double &a, double &bb);
double lab_color_diff(double l1, double a1, double b1,
                      double l2, double a2, double b2);

} // namespace tu4setup
#endif
