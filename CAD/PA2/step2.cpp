// step2.cpp  (wire_delay removed; debug-capable) -- debug code commented out
#include "step2.h"
#include "parser.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <cctype>
#include <future>

namespace sta {
static std::string upper_copy(const std::string &s) {
    std::string u = s;
    for (char &c : u) c = (char)std::toupper((unsigned char)c);
    return u;
}
// Helper: robust key lookup for maps (const overload)
template<typename MapT>
static auto find_key_robust_const(const MapT &m, const std::string &key) -> decltype(m.begin()) {
    // exact
    auto it = m.find(key);
    if (it != m.end()) return it;
    // uppercase fallback
    std::string up = upper_copy(key);
    for (auto i = m.begin(); i != m.end(); ++i) if (upper_copy(i->first) == up) return i;
    // strip trailing digits (e.g., C1 -> C)
    size_t p = key.size();
    while (p > 0 && std::isdigit((unsigned char)key[p-1])) --p;
    if (p < key.size()) {
        std::string s = key.substr(0, p);
        if (!s.empty()) {
            it = m.find(s);
            if (it != m.end()) return it;
            std::string up2 = upper_copy(s);
            for (auto i = m.begin(); i != m.end(); ++i) if (upper_copy(i->first) == up2) return i;
        }
    }
    // strip single trailing char
    if (key.size() > 1) {
        std::string s2 = key.substr(0, key.size()-1);
        it = m.find(s2);
        if (it != m.end()) return it;
        std::string up3 = upper_copy(s2);
        for (auto i = m.begin(); i != m.end(); ++i) if (upper_copy(i->first) == up3) return i;
    }
    return m.end();
}

// Try to find a pin-key in a map using robust fallbacks.
// Returns iterator to found element or map.end().
template<typename MapT>
static auto find_pin_key_robust(MapT &m, const std::string &pin) -> decltype(m.begin()) {
    // exact
    auto it = m.find(pin);
    if (it != m.end()) return it;
    // uppercase fallback
    std::string up = upper_copy(pin);
    for (auto i = m.begin(); i != m.end(); ++i) if (upper_copy(i->first) == up) return i;
    // strip trailing digits (e.g., CN -> C if many libs use that)
    // only if pin length>1 and ends with digit(s)
    size_t p = pin.size();
    while (p>0 && std::isdigit((unsigned char)pin[p-1]) ) --p;
    if (p+1 <= pin.size()) {
        std::string stripped = pin.substr(0, p);
        if (!stripped.empty()) {
            it = m.find(stripped);
            if (it != m.end()) return it;
            std::string up2 = upper_copy(stripped);
            for (auto i = m.begin(); i != m.end(); ++i) if (upper_copy(i->first) == up2) return i;
        }
    }
    // strip trailing numeric-like suffix 'N' or other single chars often used: try removing last char
    if (pin.size()>1) {
        std::string drop1 = pin.substr(0, pin.size()-1);
        it = m.find(drop1);
        if (it != m.end()) return it;
        std::string up3 = upper_copy(drop1);
        for (auto i = m.begin(); i != m.end(); ++i) if (upper_copy(i->first) == up3) return i;
    }
    return m.end();
}

// helper: clamp and linear interpolation
static double lerp(double a, double b, double t) { return a + (b - a) * t; }
// Replace existing table2d_lookup with this debug-capable version.
// Returns pair<value, debug_string>
static std::pair<double,std::string> table2d_lookup_debug(const Table2D &T, double x, double y) {
    std::ostringstream dbg;
    dbg << std::fixed << std::setprecision(6);
    if (T.idx_cap.empty() || T.idx_tran.empty() || T.vals.empty()) {
        dbg << "table empty -> return 0.0\n";
        return {0.0, dbg.str()};
    }

    auto lerp_val = [](double x1, double v1, double x2, double v2, double xq) -> double {
        if (x2 == x1) return v1;
        double t = (xq - x1) / (x2 - x1);
        return v1 + t * (v2 - v1);
    };

    dbg << "query x(cap)=" << x << " y(tran)=" << y << "\n";

    // locate indices
    size_t rx = 0;
    while (rx + 1 < T.idx_cap.size() && x > T.idx_cap[rx + 1]) ++rx;
    size_t cy = 0;
    while (cy + 1 < T.idx_tran.size() && y > T.idx_tran[cy + 1]) ++cy;

    bool x_above = (x > T.idx_cap.back());
    bool y_above = (y > T.idx_tran.back());
    dbg << "loc rx=" << rx << " (cap[idx]=" << T.idx_cap[rx] << ")"
        << "  cy=" << cy << " (tran[idx]=" << T.idx_tran[cy] << ")\n";
    dbg << "x_above=" << (x_above ? "yes":"no") << "  y_above=" << (y_above ? "yes":"no") << "\n";

    auto table_val = [&](size_t r, size_t c)->double{
        if (r < T.vals.size() && c < T.vals[r].size()) return T.vals[r][c];
        return 0.0;
    };

    // Case: inside bounds -> bilinear interp
    if (!x_above && !y_above) {
        size_t r1 = rx;
        size_t r2 = (rx + 1 < T.idx_cap.size()) ? rx + 1 : rx;
        size_t c1 = cy;
        size_t c2 = (cy + 1 < T.idx_tran.size()) ? cy + 1 : cy;

        double v11 = table_val(r1, c1);
        double v12 = table_val(r1, c2);
        double v21 = table_val(r2, c1);
        double v22 = table_val(r2, c2);

        double x1 = T.idx_cap[r1];
        double x2 = T.idx_cap[r2];
        double y1 = T.idx_tran[c1];
        double y2 = T.idx_tran[c2];

        double tx = (x2 == x1) ? 0.0 : ((x - x1) / (x2 - x1));
        double ty = (y2 == y1) ? 0.0 : ((y - y1) / (y2 - y1));
        if (tx < 0) tx = 0; if (tx > 1) tx = 1;
        if (ty < 0) ty = 0; if (ty > 1) ty = 1;

        dbg << "BILINEAR INTERP\n";
        dbg << "  r1=" << r1 << " r2=" << r2 << " c1=" << c1 << " c2=" << c2 << "\n";
        dbg << "  corner v11=" << v11 << " v12=" << v12 << " v21=" << v21 << " v22=" << v22 << "\n";
        dbg << "  x1=" << x1 << " x2=" << x2 << " y1=" << y1 << " y2=" << y2 << "\n";
        dbg << "  tx=" << tx << " ty=" << ty << "\n";

        double a = v11 + tx * (v21 - v11);
        double b = v12 + tx * (v22 - v12);
        double v = a + ty * (b - a);
        dbg << "  interp a=" << a << " b=" << b << " result=" << v << "\n";
        return {v, dbg.str()};
    }

    // Case: x above, y within -> interpolate in y on last two rows, then extrapolate in x
    if (x_above && !y_above) {
        size_t ncap = T.idx_cap.size();
        dbg << "X ABOVE: use last two rows for extrapolation\n";
        if (ncap == 1) {
            size_t c1 = cy;
            size_t c2 = (cy + 1 < T.idx_tran.size()) ? cy + 1 : cy;
            double v1 = table_val(0, c1);
            double v2 = table_val(0, c2);
            double y1 = T.idx_tran[c1];
            double y2 = T.idx_tran[c2];
            double ty = (y2 == y1) ? 0.0 : ((y - y1) / (y2 - y1));
            if (ty < 0) ty = 0; if (ty > 1) ty = 1;
            double val = v1 + ty * (v2 - v1);
            dbg << "  only one row: val at y = " << val << "\n";
            return {val, dbg.str()};
        }
        size_t rA = ncap - 2;
        size_t rB = ncap - 1;
        size_t c1 = cy;
        size_t c2 = (cy + 1 < T.idx_tran.size()) ? cy + 1 : cy;
        double vA1 = table_val(rA, c1);
        double vA2 = table_val(rA, c2);
        double vB1 = table_val(rB, c1);
        double vB2 = table_val(rB, c2);
        double y1 = T.idx_tran[c1];
        double y2 = T.idx_tran[c2];
        double ty = (y2 == y1) ? 0.0 : ((y - y1) / (y2 - y1));
        if (ty < 0) ty = 0; if (ty > 1) ty = 1;
        double valA = vA1 + ty * (vA2 - vA1);
        double valB = vB1 + ty * (vB2 - vB1);
        double xA = T.idx_cap[rA];
        double xB = T.idx_cap[rB];
        double val = lerp_val(xA, valA, xB, valB, x);
        dbg << "  rows rA=" << rA << " rB=" << rB << "  vals at y: valA=" << valA << " valB=" << valB << "\n";
        dbg << "  xA=" << xA << " xB=" << xB << " extrapolated result=" << val << "\n";
        return {val, dbg.str()};
    }

    // Case: y above, x within -> interpolate in x then extrapolate in y
    if (y_above && !x_above) {
        size_t ntr = T.idx_tran.size();
        dbg << "Y ABOVE: use last two cols for extrapolation\n";
        if (ntr == 1) {
            size_t r1 = rx;
            size_t r2 = (rx + 1 < T.idx_cap.size()) ? rx + 1 : rx;
            double v1 = table_val(r1, 0);
            double v2 = table_val(r2, 0);
            double x1 = T.idx_cap[r1];
            double x2 = T.idx_cap[r2];
            double tx = (x2 == x1) ? 0.0 : ((x - x1) / (x2 - x1));
            if (tx < 0) tx = 0; if (tx > 1) tx = 1;
            double val = v1 + tx * (v2 - v1);
            dbg << "  only one col: val at x = " << val << "\n";
            return {val, dbg.str()};
        }
        size_t cA = ntr - 2;
        size_t cB = ntr - 1;
        size_t r1 = rx;
        size_t r2 = (rx + 1 < T.idx_cap.size()) ? rx + 1 : rx;
        double vA1 = table_val(r1, cA);
        double vA2 = table_val(r2, cA);
        double vB1 = table_val(r1, cB);
        double vB2 = table_val(r2, cB);
        double x1 = T.idx_cap[r1];
        double x2 = T.idx_cap[r2];
        double tx = (x2 == x1) ? 0.0 : ((x - x1) / (x2 - x1));
        if (tx < 0) tx = 0; if (tx > 1) tx = 1;
        double valA = vA1 + tx * (vA2 - vA1);
        double valB = vB1 + tx * (vB2 - vB1);
        double yA = T.idx_tran[cA];
        double yB = T.idx_tran[cB];
        double val = lerp_val(yA, valA, yB, valB, y);
        dbg << "  cols cA=" << cA << " cB=" << cB << "  vals at x: valA=" << valA << " valB=" << valB << "\n";
        dbg << "  yA=" << yA << " yB=" << yB << " extrapolated result=" << val << "\n";
        return {val, dbg.str()};
    }

    // Case: both above -> use last 2x2 corner, do bilinear on that corner at requested y (via ty_local)
    {
        size_t ncap = T.idx_cap.size();
        size_t ntr = T.idx_tran.size();
        dbg << "BOTH AXES ABOVE: extrapolate from corner\n";
        if (ncap == 1 && ntr == 1) {
            double v = table_val(0,0);
            dbg << "  single point table -> " << v << "\n";
            return {v, dbg.str()};
        }
        size_t rA = (ncap >= 2) ? ncap - 2 : ncap - 1;
        size_t rB = ncap - 1;
        size_t cA = (ntr >= 2) ? ntr - 2 : ntr - 1;
        size_t cB = ntr - 1;

        double vAA = table_val(rA, cA);
        double vAB = table_val(rA, cB);
        double vBA = table_val(rB, cA);
        double vBB = table_val(rB, cB);

        double xA = T.idx_cap[rA];
        double xB = T.idx_cap[rB];
        double yA = T.idx_tran[cA];
        double yB = T.idx_tran[cB];

        dbg << "  corner indices rA=" << rA << " rB=" << rB << " cA=" << cA << " cB=" << cB << "\n";
        dbg << "  corner vals vAA=" << vAA << " vAB=" << vAB << " vBA=" << vBA << " vBB=" << vBB << "\n";
        double ty_local = (yB == yA) ? 0.0 : ((y - yA) / (yB - yA));
        if (ty_local < 0) ty_local = 0; if (ty_local > 1) ty_local = 1;
        double val_xA_at_y = vAA + ty_local * (vAB - vAA);
        double val_xB_at_y = vBA + ty_local * (vBB - vBA);
        dbg << "  ty_local=" << ty_local << " val_xA_at_y=" << val_xA_at_y << " val_xB_at_y=" << val_xB_at_y << "\n";
        double val_at_xy = lerp_val(xA, val_xA_at_y, xB, val_xB_at_y, x);
        dbg << "  extrapolated value=" << val_at_xy << "\n";
        return {val_at_xy, dbg.str()};
    }
}

static double table2d_lookup(const Table2D &T, double x, double y) {
    const auto &xs = T.idx_cap;    // x 軸（總負載）
    const auto &ys = T.idx_tran;   // y 軸（input transition）
    const auto &v  = T.vals;       // v[i][j] = f(xs[i], ys[j])

    const size_t nx = xs.size();
    const size_t ny = ys.size();

    // 基本防呆
    if (nx < 2 || ny < 2) {
        // 無法用兩點做線性(外)插，回傳單一點或 0。這裡選回 vals[0][0]（若存在）。
        return (nx && ny && !v.empty() && !v[0].empty()) ? v[0][0] : 0.0;
    }
#ifndef NDEBUG
    // 可選：在 Debug 時檢查遞增與維度一致
    for (size_t i = 1; i < nx; ++i) { assert(xs[i] > xs[i-1]); }
    for (size_t j = 1; j < ny; ++j) { assert(ys[j] > ys[j-1]); }
    assert(v.size() == nx);
    for (size_t i = 0; i < nx; ++i) { assert(v[i].size() == ny); }
#endif

    auto find_segment = [](const std::vector<double>& idx, double val) {
        struct Seg { int lo, hi; double t; };
        const int n = static_cast<int>(idx.size());
        // 使用 lower_bound 找到第一個 >= val 的位置
        auto it = std::lower_bound(idx.begin(), idx.end(), val);
        if (it == idx.begin()) {
            // 在下界（含等於第一點）：用前兩點外推 / 內插
            int lo = 0, hi = 1;
            double t = (val - idx[lo]) / (idx[hi] - idx[lo]);
            return Seg{lo, hi, t};
        } else if (it == idx.end()) {
            // 在上界之外：用最後兩點外推
            int lo = n - 2, hi = n - 1;
            double t = (val - idx[lo]) / (idx[hi] - idx[lo]);
            return Seg{lo, hi, t};
        } else {
            // 內部區間：idx[i-1] <= val <= idx[i]
            int hi = static_cast<int>(it - idx.begin());
            int lo = hi - 1;
            // 避免除 0（理論上不會，因為假設嚴格遞增）
            double denom = (idx[hi] - idx[lo]);
            double t = denom != 0.0 ? (val - idx[lo]) / denom : 0.0;
            return Seg{lo, hi, t};
        }
    };

    auto lerp = [](double a, double b, double t) {
        return a + (b - a) * t; // t 可小於 0 或大於 1，代表外推
    };

    // 找到 x 與 y 的左右/上下兩點與內插參數
    auto sx = find_segment(xs, x);
    auto sy = find_segment(ys, y);

    // 取四個角點
    double v00 = v[sy.lo][sx.lo];
double v01 = v[sy.hi][sx.lo];
double v10 = v[sy.lo][sx.hi];
double v11 = v[sy.hi][sx.hi];

    // 先沿 x 內插兩條，再沿 y 內插一次（雙線性）
    double a0 = lerp(v00, v10, sx.t);
    double a1 = lerp(v01, v11, sx.t);
    double f  = lerp(a0,  a1,  sy.t);
    return f;
}


// -------------------- topo order indices helper --------------------
static std::vector<size_t> topo_order_indices(const NetlistModel &M) {
    std::vector<std::pair<int,size_t>> vec;
    vec.reserve(M.instances.size());
    for (size_t i=0;i<M.instances.size();++i) vec.emplace_back(M.instances[i].topo_index, i);
    std::sort(vec.begin(), vec.end(), [](auto &a, auto &b){ return a.first < b.first; });
    std::vector<size_t> out; out.reserve(vec.size());
    for (auto &p: vec) out.push_back(p.second);
    return out;
}
// -------------------- Evaluate cell logic --------------------
static int evaluate_cell_logic(const std::string &type, const std::vector<int> &in_vals) {
    // normalize base name: remove digits and drive suffixes, uppercase
    std::string t;
    for (char c : type) {
        if (!std::isdigit((unsigned char)c)) t.push_back(std::toupper((unsigned char)c));
    }
    // simple matches
    if (t.find("INV") != std::string::npos) {
        return in_vals.empty() ? 0 : (in_vals[0] ? 0 : 1);
    }
    if (t.find("BUF") != std::string::npos) {
        return in_vals.empty() ? 0 : in_vals[0];
    }
    if (t.find("NAND") != std::string::npos) {
        int a = 1;
        for (int v : in_vals) a &= (v?1:0);
        return a ? 0 : 1;
    }
    if (t.find("AND") != std::string::npos) {
        int a = 1;
        for (int v : in_vals) a &= (v?1:0);
        return a;
    }
    if (t.find("NOR") != std::string::npos) {
        int a = 0;
        for (int v : in_vals) a |= (v?1:0);
        return a ? 0 : 1;
    }
    if (t.find("OR") != std::string::npos) {
        int a = 0;
        for (int v : in_vals) a |= (v?1:0);
        return a;
    }
    if (t.find("XNOR") != std::string::npos) {
        int a = 0;
        for (int v : in_vals) a ^= (v?1:0);
        return a ? 0 : 1;
    }
    if (t.find("XOR") != std::string::npos) {
        int a = 0;
        for (int v : in_vals) a ^= (v?1:0);
        return a;
    }
    // fallback majority
    int ones = 0;
    for (int v : in_vals) if (v) ++ones;
    return (ones * 2 >= (int)in_vals.size()) ? 1 : 0;
}
// -------------------- pick controlling input (final rules) --------------------

// -------------------- pick controlling input (latest-arrival fallback) --------------------
// Replace existing pick_controlling_input with this version:
// - NAND -> controlling value 0
// - NOR  -> controlling value 1
// - If controlling candidates exist: choose earliest arrival; tie-break smaller transition
// - Else fallback: choose latest arrival; tie-break larger transition
static int pick_controlling_input(const Instance &inst,
    const std::vector<int> &input_values,
    const std::vector<double> &candidate_arrivals,
    const std::vector<double> &candidate_transitions) {

    // determine controlling value specifically for NAND or NOR
    std::string t;
    for (char c: inst.type) if (!std::isdigit((unsigned char)c)) t.push_back(std::toupper((unsigned char)c));
    int controlling_value_known = -1;
    if (t.find("NAND") != std::string::npos) controlling_value_known = 0;
    else if (t.find("NOR") != std::string::npos) controlling_value_known = 1;

    int chosen = -1;
    // If there is a known controlling value (only NAND or NOR here), select among those inputs:
    // choose the candidate with smallest arrival (earliest). If tied within eps,
    // pick the one with smaller transition (tie-break: smaller transition).
    if (controlling_value_known != -1) {
        for (size_t i = 0; i < input_values.size(); ++i) {
            if (input_values[i] == controlling_value_known) {
                if (chosen == -1) chosen = (int)i;
                else {
                    double ai = candidate_arrivals[i], aj = candidate_arrivals[chosen];
                    if (ai < aj - 1e-12) chosen = (int)i;
                    else if (std::abs(ai - aj) <= 1e-12) {
                        if (candidate_transitions[i] < candidate_transitions[chosen]) chosen = (int)i;
                    }
                }
            }
        }
        if (chosen != -1) return chosen;
    }

    // Fallback: choose latest-arrival (max). If tied, choose the one with larger transition.
    for (size_t i = 0; i < candidate_arrivals.size(); ++i) {
        if (chosen == -1) chosen = (int)i;
        else {
            double ai = candidate_arrivals[i], aj = candidate_arrivals[chosen];
            if (ai > aj + 1e-12) chosen = (int)i;
            else if (std::abs(ai - aj) <= 1e-12) {
                if (candidate_transitions[i] > candidate_transitions[chosen]) chosen = (int)i;
            }
        }
    }
    return chosen;
}


static int pick_controlling_input_latest(const Instance &inst,
    const std::vector<int> &input_values,
    const std::vector<double> &candidate_arrivals,
    const std::vector<double> &candidate_transitions) {

    std::string t;
    for (char c: inst.type) if (!std::isdigit((unsigned char)c)) t.push_back(std::toupper((unsigned char)c));
    int controlling_value_known = -1;
    if (t.find("NAND")!=std::string::npos || t.find("NOR")!=std::string::npos) controlling_value_known = 0;
    else if (t.find("AND")!=std::string::npos || t.find("OR")!=std::string::npos) controlling_value_known = 1;

    int chosen=-1;
    if (controlling_value_known != -1) {
        for (size_t i=0;i<input_values.size();++i) {
            if (input_values[i] == controlling_value_known) {
                if (chosen==-1) chosen=(int)i;
                else {
                    if (candidate_arrivals[i] > candidate_arrivals[chosen] + 1e-12) chosen=(int)i;
                    else if (std::abs(candidate_arrivals[i]-candidate_arrivals[chosen])<=1e-12 && candidate_transitions[i] > candidate_transitions[chosen]) chosen=(int)i;
                }
            }
        }
        if (chosen != -1) return chosen;
    }
    // fallback: latest-arrival (max), tie-break by transition
    for (size_t i=0;i<candidate_arrivals.size();++i) {
        if (chosen==-1) chosen=(int)i;
        else {
            if (candidate_arrivals[i] > candidate_arrivals[chosen] + 1e-12) chosen=(int)i;
            else if (std::abs(candidate_arrivals[i]-candidate_arrivals[chosen])<=1e-12 && candidate_transitions[i] > candidate_transitions[chosen]) chosen=(int)i;
        }
    }
    return chosen;
}
// -------------------- main combined routine (Step2+Step3+Step4) --------------------
bool run_step2and3(ParseResult &R, double wire_delay, const std::string &out_dir, bool debug) {
    NetlistModel &M = R.netlist;
    // sanity: topo_index should be set (we skip explicit cycle detection per your decision)
    for (const auto &inst : M.instances) {
        if (inst.topo_index == -1) return false;
    }

    std::unordered_map<std::string, size_t> inst_index;
    for (size_t i=0;i<M.instances.size();++i) inst_index[M.instances[i].name] = i;

    // initialize PI nets reachable and their arrival/transition (PI arrival=0, transition=0)
    for (const auto &pi : M.primary_inputs) {
        auto it = M.nets.find(pi);
        if (it != M.nets.end()) {
            it->second.arrival_time = 0.0;
            it->second.driven_transition = 0.0;
            it->second.reachable = true;
            it->second.pred_net.clear();
        } else {
            // create if missing
            Net n; n.name = pi; n.driver_instance = "PI"; n.arrival_time = 0.0; n.driven_transition = 0.0; n.reachable = true; n.pred_net.clear();
            M.nets[pi] = n;
        }
    }

    auto order = topo_order_indices(M);

    // prepare debug file
    // /* DEBUG: debug file creation commented out
    // std::ofstream dbgofs;
    // if (debug) {
    //     std::string libb = R.lib_basename.empty() ? "lib" : R.lib_basename;
    //     std::string netb = R.net_basename.empty() ? "net" : R.net_basename;
    //     std::string dbgname = libb + "_" + netb + "_timing_debug.txt";
    //     std::string dbgpath = out_dir.empty() ? dbgname : (out_dir + "/" + dbgname);
    //     dbgofs.open(dbgpath);
    //     if (dbgofs.is_open()) dbgofs << std::fixed << std::setprecision(6);
    // }
    // */

    // STEP2+STEP3: compute timing as before (single topo pass)
    for (size_t idx : order) {
        Instance &inst = M.instances[idx];

        // collect inputs: determine input_arrival (max of candidate arrivals) and
        // choose the latest-arrival input's transition as input_transition by default.
        double input_arrival = -std::numeric_limits<double>::infinity();
        double chosen_input_transition = 0.0;
        double latest_input_arrival = -std::numeric_limits<double>::infinity();
        std::string chosen_latest_net;
        bool any_input = false;

        std::ostringstream inst_dbg;
        inst_dbg << "Instance: " << inst.name << "\n";
        inst_dbg << "  topo_index = " << inst.topo_index << " type = " << inst.type << "\n";

        // For collecting arrays used later by Step4 for controlling selection
        std::vector<std::string> in_net_names;
        std::vector<double> candidate_arrivals;
        std::vector<double> candidate_transitions;
        std::vector<int> dummy_input_values; // placeholder; actual logic values set in Step4 per pattern
////////////////////////////////////////////////
for (const auto &pn : inst.pin_net) {
    const std::string &pin = pn.first;
    const std::string &netname = pn.second;

    // skip the instance's chosen output pin (robustly)
    bool is_out_here = false;
    if (!inst.output_pin.empty()) {
        if (pin == inst.output_pin || upper_copy(pin) == upper_copy(inst.output_pin)) is_out_here = true;
    } else {
        // if no chosen output stored, fallback: check lib pin_dir (if available)
        auto cit = R.cells.find(inst.type);
        if (cit == R.cells.end()) cit = R.cells.find(upper_copy(inst.type));
        if (cit != R.cells.end()) {
            // try to see if this pin name corresponds to an output in lib
            auto dit = cit->second.pin_dir.find(pin);
            if (dit == cit->second.pin_dir.end()) {
                // try robust match against cd.pin_dir keys
                auto pit = find_pin_key_robust(cit->second.pin_dir, pin);
                if (pit != cit->second.pin_dir.end()) dit = pit;
            }
            if (dit != cit->second.pin_dir.end() && dit->second == "output") is_out_here = true;
        } else {
            // legacy: consider Z/ZN literal as outputs
            if (pin == "ZN" || pin == "Z" || upper_copy(pin) == "ZN" || upper_copy(pin) == "Z") is_out_here = true;
        }
    }
    if (is_out_here) continue; // skip outputs
 
///////////////////////////////////////////////

            auto nit = M.nets.find(netname);
            if (nit == M.nets.end()) {
                // if (dbgofs.is_open()) { dbgofs << inst_dbg.str(); dbgofs << "  ERROR: input net " << netname << " not found\n\n"; }
                return false;
            }
            const Net &net = nit->second;
            if (!net.reachable) {
                // if (dbgofs.is_open()) { dbgofs << inst_dbg.str(); dbgofs << "  ERROR: input net " << netname << " not reachable (driver arrival undefined)\n\n"; }
                return false;
            }

            double cand_arr = net.arrival_time;
            if (!(net.driver_instance == "PI" || net.driver_instance.empty())) {
                cand_arr = net.arrival_time + wire_delay;
            }
            double cand_tran = net.driven_transition;

            inst_dbg << "  input pin " << pin << " net " << netname
                    << " driver=" << net.driver_instance
                    << " net.arrival=" << net.arrival_time
                    << " cand_arr=" << cand_arr
                    << " cand_tran=" << cand_tran << "\n";

            any_input = true;
            in_net_names.push_back(netname);
            candidate_arrivals.push_back(cand_arr);
            candidate_transitions.push_back(cand_tran);
            dummy_input_values.push_back(0); // placeholder; real values assigned per pattern in Step4

            // input_arrival used for output timing is the worst (max) of candidate arrivals
            if (cand_arr > input_arrival) input_arrival = cand_arr;

            // default chosen_input_transition: latest-arrival fallback (matches earlier behavior)
            if (cand_arr > latest_input_arrival) {
                latest_input_arrival = cand_arr;
                chosen_input_transition = cand_tran;
                chosen_latest_net = netname;
            } else if (std::abs(cand_arr - latest_input_arrival) <= 1e-12) {
                if (cand_tran > chosen_input_transition) {
                    chosen_input_transition = cand_tran;
                    chosen_latest_net = netname;
                }
            }
        } // end input loop

        if (!any_input) {
            input_arrival = 0.0;
            chosen_input_transition = 0.0;
            chosen_latest_net.clear();
            inst_dbg << "  note: no inputs -> input_arrival=0 input_transition=0\n";
        }

        inst_dbg << "  computed input_arrival = " << input_arrival
                << " latest_input_arrival = " << latest_input_arrival
                << " chosen_input_transition(default) = " << chosen_input_transition
                << " chosen_latest_net = " << chosen_latest_net << "\n";
        // record Step2 baseline chosen input net (controlling input) on the instance
inst.chosen_input_net = chosen_latest_net;

// optional: store pred_instance (driver of the chosen input net) for convenience
if (!chosen_latest_net.empty()) {
    auto nit2 = M.nets.find(chosen_latest_net);
    if (nit2 != M.nets.end()) inst.pred_instance = nit2->second.driver_instance;
    else inst.pred_instance.clear();
} else {
    inst.pred_instance.clear();
}


        // lookup cell timing from library using default chosen_input_transition (will be overwritten in Step4 per pattern if needed)
        double pd_rise = 0.0, ot_rise = 0.0, pd_fall = 0.0, ot_fall = 0.0;
        auto cit = R.cells.find(inst.type);
        if (cit != R.cells.end()) {
            const CellDef &cd = cit->second;
            pd_rise = table2d_lookup(cd.cell_rise, inst.output_loading, chosen_input_transition);
            ot_rise = table2d_lookup(cd.rise_tran, inst.output_loading, chosen_input_transition);
            pd_fall = table2d_lookup(cd.cell_fall, inst.output_loading, chosen_input_transition);
            ot_fall = table2d_lookup(cd.fall_tran, inst.output_loading, chosen_input_transition);
            inst_dbg << "  table lookup (load=" << inst.output_loading << " y=" << chosen_input_transition << ")\n";
            inst_dbg << "    pd_rise=" << pd_rise << " ot_rise=" << ot_rise << "\n";
            inst_dbg << "    pd_fall=" << pd_fall << " ot_fall=" << ot_fall << "\n";
        } else {
            inst_dbg << "  WARNING: cell type " << inst.type << " not found in library, pd/ot=0\n";
        }

        // persist per-instance pd/ot results (default using latest-arrival chosen transition)
        inst.pd_rise = pd_rise;
        inst.ot_rise = ot_rise;
        inst.pd_fall = pd_fall;
        inst.ot_fall = ot_fall;

        // choose worst-case pd for timing propagation (this uses default chosen transition)
        char worst = '1';
        double chosen_pd = pd_rise;
        double chosen_ot = ot_rise;
        if (pd_fall > pd_rise) {
            worst = '0';
            chosen_pd = pd_fall;
            chosen_ot = ot_fall;
        }
        inst.prop_delay = chosen_pd;
        inst.output_transition = chosen_ot;
        inst.worst_output = worst;

        inst_dbg << "  chosen worst_output(default) = " << worst << " prop_delay=" << chosen_pd << " output_transition=" << chosen_ot << "\n";

        // write output net arrival
std::string outnet = inst.output_net;
double out_arrival = input_arrival + chosen_pd;
if (!outnet.empty()) {
    auto nit = M.nets.find(outnet);
    if (nit == M.nets.end()) {
        Net n;
        n.name = outnet;
        n.driver_instance = inst.name;
        n.arrival_time = out_arrival;
        n.driven_transition = chosen_ot;
        n.reachable = true;
        n.pred_net = inst.chosen_input_net; // <-- use chosen input from Step2
        M.nets[outnet] = n;
    } else {
        Net &nout = nit->second;
        nout.arrival_time = out_arrival;
        nout.driven_transition = chosen_ot;
        nout.reachable = true;
        nout.pred_net = inst.chosen_input_net; // <-- use chosen input from Step2
    }
    inst_dbg << "  wrote out net " << outnet << " arrival=" << out_arrival << " driven_tran=" << chosen_ot << "\n";
} else {
    inst_dbg << "  note: instance has no output_net\n";
}



        // DEBUG writes commented out
        // if (debug && dbgofs.is_open()) dbgofs << inst_dbg.str() << "\n";
    } // end topo loop for timing

    // write timing.txt (instance-level results) - sorted by name consistent with previous behavior
    std::string libb = R.lib_basename.empty() ? "lib" : R.lib_basename;
    std::string netb = R.net_basename.empty() ? "net" : R.net_basename;
    std::string fname = libb + "_" + netb + "_delay.txt";
    std::string outfname = out_dir.empty() ? fname : (out_dir + "/" + fname);
    std::ofstream ofs(outfname);
    if (!ofs.is_open()) {
        // if (dbgofs.is_open()) dbgofs.close();
        return false;
    }
    ofs << std::fixed << std::setprecision(6);

    // prepare name-sorted order
    std::vector<std::pair<std::string,size_t>> name_idx;
    for (size_t i=0;i<M.instances.size();++i) {
        if (!M.module_name.empty() && M.instances[i].name == M.module_name) continue;
        name_idx.emplace_back(M.instances[i].name, i);
    }
    auto name_to_int_local = [](const std::string &s)->int {
        std::string num; for (char c: s) if (std::isdigit((unsigned char)c)) num.push_back(c);
        return num.empty()?0:std::stoi(num);
    };
    std::sort(name_idx.begin(), name_idx.end(),
        [&](const std::pair<std::string,size_t> &a, const std::pair<std::string,size_t> &b){
            int ia = name_to_int_local(a.first), ib = name_to_int_local(b.first);
            if (ia != ib) return ia < ib;
            return a.first < b.first;
    });
    for (auto &p : name_idx) {
        size_t i = p.second;
        ofs << p.first << " " << M.instances[i].worst_output << " " << M.instances[i].prop_delay << " " << M.instances[i].output_transition << "\n";
    }
    ofs.close();

    // Step3: find longest and shortest delays among primary outputs
    std::vector<std::string> reachable_po;
    for (const auto &po : M.primary_outputs) {
        auto nit = M.nets.find(po);
        if (nit != M.nets.end() && nit->second.reachable) reachable_po.push_back(po);
    }
    if (reachable_po.empty()) {
        // if (dbgofs.is_open()) dbgofs << "ERROR: no reachable primary outputs found\n";
        // if (dbgofs.is_open()) dbgofs.close();
        return false;
    }

    // find candidate longest and shortest with tie-breaker by output_transition larger
    auto pick_best = [&](const std::vector<std::string> &cands, bool pick_max)->std::string{
        std::string best = cands.front();
        double best_val = M.nets[best].arrival_time;
        double best_tran = M.nets[best].driven_transition;
        for (size_t i=1;i<cands.size();++i) {
            const std::string &n = cands[i];
            double val = M.nets[n].arrival_time;
            double tran = M.nets[n].driven_transition;
            if (pick_max) {
                if (val > best_val + 1e-12) { best = n; best_val = val; best_tran = tran; }
                else if (std::abs(val - best_val) < 1e-12 && tran > best_tran) { best = n; best_val = val; best_tran = tran; }
            } else {
                if (val < best_val - 1e-12) { best = n; best_val = val; best_tran = tran; }
                else if (std::abs(val - best_val) < 1e-12 && tran > best_tran) { best = n; best_val = val; best_tran = tran; }
            }
        }
        return best;
    };

    std::string longest_net = pick_best(reachable_po, true);
    std::string shortest_net = pick_best(reachable_po, false);

    // backtrack net-only paths (from leaf back to PI or empty pred)
    auto backtrack_path = [&](const std::string &leaf)->std::vector<std::string>{
        std::vector<std::string> path;
        std::string cur = leaf;
        std::unordered_set<std::string> seen;
        while(!cur.empty()) {
            if (seen.count(cur)) break; // defensive only
            seen.insert(cur);
            path.push_back(cur);
            auto nit = M.nets.find(cur);
            if (nit == M.nets.end()) break;
            std::string pred = nit->second.pred_net;
            if (pred.empty()) break;
            cur = pred;
        }
        std::reverse(path.begin(), path.end()); // from source -> leaf
        return path;
    };

    auto longest_path = backtrack_path(longest_net);
    auto shortest_path = backtrack_path(shortest_net);

    // write path file
    {
        std::string libname = R.lib_basename.empty() ? "lib" : R.lib_basename;
        std::string casename = R.net_basename.empty() ? "case" : R.net_basename;
        std::string path_fname = libname + "_" + casename + "_path.txt";
        std::string path_out = out_dir.empty() ? path_fname : (out_dir + "/" + path_fname);
        std::ofstream pofs(path_out);
        if (!pofs.is_open()) {
            // if (dbgofs.is_open()) dbgofs << "WARNING: cannot open path output file " << path_out << "\n";
        } else {
            pofs << std::fixed << std::setprecision(6);
            double longest_val = M.nets[longest_net].arrival_time;
            pofs << "Longest delay = " << longest_val << ", the path is: ";
            for (size_t i = 0; i < longest_path.size(); ++i) {
                if (i) pofs << " -> ";
                pofs << longest_path[i];
            }
            pofs << "\n";
            double shortest_val = M.nets[shortest_net].arrival_time;
            pofs << "Shortest delay = " << shortest_val << ", the path is: ";
            for (size_t i = 0; i < shortest_path.size(); ++i) {
                if (i) pofs << " -> ";
                pofs << shortest_path[i];
            }
            pofs << "\n";
            pofs.close();
        }
    }
// -------------------- STEP4: parallel per-pattern using std::async (integrated, ready to paste) --------------------



// Step4 body (use inside same translation unit where M, R, order, out_dir, wire_delay are visible)
{
    // patterns: vector<unordered_map<string,int>>
    std::vector<std::unordered_map<std::string,int>> patterns;
    if (!R.input_patterns.empty()) patterns = R.input_patterns;
    else {
        std::unordered_map<std::string,int> p;
        for (const auto &pi : M.primary_inputs) p[pi] = 0;
        patterns.push_back(std::move(p));
    }

    // gate_info output path (truncate first)
    std::string libname = R.lib_basename.empty() ? "lib" : R.lib_basename;
    std::string casename = R.net_basename.empty() ? "case" : R.net_basename;
    std::string gfname = libname + "_" + casename + "_gate_info.txt";
    std::string gpath = out_dir.empty() ? gfname : (out_dir + "/" + gfname);
    { std::ofstream t(gpath, std::ios::trunc); t.close(); }

    // parsed-order list for PHASE B (pair(parsed_index, instance_index))
    std::vector<std::pair<int,size_t>> order_by_parsed;
    order_by_parsed.reserve(M.instances.size());
    for (size_t i = 0; i < M.instances.size(); ++i) order_by_parsed.emplace_back(M.instances[i].parsed_index, i);
    std::sort(order_by_parsed.begin(), order_by_parsed.end(), [](auto &a, auto &b){ return a.first < b.first; });

    // prepare concurrency
    size_t P = patterns.size();
    unsigned int hw = std::thread::hardware_concurrency();
    size_t max_concurrency = (hw == 0) ? 2 : std::max<unsigned int>(1, hw);
    std::vector<std::future<std::string>> futs;
    futs.reserve(P);

    // capture order (topo order indices) must be available
    // 'order' is assumed computed previously (topo traversal order)
    std::vector<size_t> order_local = topo_order_indices(M); // if 'order' is local, capture or pass appropriately

    // worker lambda: computes gate_info text for a single pattern index and returns string
    auto worker = [&, order_local](size_t pidx)->std::string {
    const auto &pat = patterns[pidx];

        // pattern-local net arrival/transition maps (copy from global baseline)
        std::unordered_map<std::string,double> net_arrival_local;
        std::unordered_map<std::string,double> net_tran_local;
        net_arrival_local.reserve(M.nets.size()*2);
        net_tran_local.reserve(M.nets.size()*2);
        for (const auto &kv : M.nets) {
            net_arrival_local[kv.first] = kv.second.arrival_time;
            net_tran_local[kv.first]   = kv.second.driven_transition;
        }

        // pattern-local logic: initialize PIs
        std::unordered_map<std::string,int> net_logic;
        net_logic.reserve(M.primary_inputs.size()*2 + 16);
        for (const auto &pi : M.primary_inputs) {
            auto it = pat.find(pi);
            net_logic[pi] = (it != pat.end()) ? it->second : 0;
        }

        // saved results per-instance (indexed by instance index in M.instances)
        size_t Ninst = M.instances.size();
        std::vector<double> saved_cell_delay(Ninst, 0.0);
        std::vector<double> saved_cell_tran(Ninst, 0.0);
        std::vector<int>    saved_logic_value(Ninst, 0);
        std::vector<std::string> saved_chosen_net(Ninst);
        std::vector<double> saved_pd_r(Ninst, 0.0), saved_pd_f(Ninst, 0.0), saved_ot_r(Ninst, 0.0), saved_ot_f(Ninst, 0.0);

        // PHASE A: traverse in topo order
        for (size_t idx : order_local) {
            if (idx >= M.instances.size()) continue;
            const auto &inst_ro = M.instances[idx]; // const reference for read-only fields

            // --- Build ordered input pin list using library pin_list (fallback to inst_ro.pin_net keys) ---
            std::vector<std::string> ordered_input_pins;    // pin keys present on instance
            std::vector<std::string> ordered_input_netnames; // corresponding net names
            ordered_input_pins.reserve(inst_ro.pin_net.size());
            ordered_input_netnames.reserve(inst_ro.pin_net.size());

            // try to find cell definition to get pin order
            auto cit = R.cells.find(inst_ro.type);
            if (cit == R.cells.end()) cit = R.cells.find(upper_copy(inst_ro.type));

            if (cit != R.cells.end() && !cit->second.pin_list.empty()) {
                const auto &pin_list = cit->second.pin_list;
                for (const auto &libpin : pin_list) {
                    // skip output pins by checking pin_dir if available
                    auto dit = cit->second.pin_dir.find(libpin);
                    if (dit != cit->second.pin_dir.end() && dit->second == "output") continue;

                    // find which key in inst_ro.pin_net corresponds to this lib pin
                    auto found = find_key_robust_const(inst_ro.pin_net, libpin);
                    if (found != inst_ro.pin_net.end()) {
                        ordered_input_pins.push_back(found->first);
                        ordered_input_netnames.push_back(found->second);
                    } else {
                        // not present on instance -> skip
                    }
                }
                // fallback: if none found, iterate instance pin map deterministically
                if (ordered_input_netnames.empty()) {
                    std::vector<std::string> keys;
                    keys.reserve(inst_ro.pin_net.size());
                    for (const auto &pn : inst_ro.pin_net) {
                        if (upper_copy(pn.first) == "ZN" || upper_copy(pn.first) == "Z") continue;
                        keys.push_back(pn.first);
                    }
                    std::sort(keys.begin(), keys.end());
                    for (const auto &k : keys) {
                        ordered_input_pins.push_back(k);
                        ordered_input_netnames.push_back(inst_ro.pin_net.at(k));
                    }
                }
            } else {
                // no library order: fall back to stable ordering of instance's pin keys
                std::vector<std::string> keys;
                keys.reserve(inst_ro.pin_net.size());
                for (const auto &pn : inst_ro.pin_net) {
                    if (upper_copy(pn.first) == "ZN" || upper_copy(pn.first) == "Z") continue;
                    keys.push_back(pn.first);
                }
                std::sort(keys.begin(), keys.end());
                for (const auto &k : keys) {
                    ordered_input_pins.push_back(k);
                    ordered_input_netnames.push_back(inst_ro.pin_net.at(k));
                }
            }

            // --- (A) logic evaluation for this pattern using ordered inputs ---
            std::vector<int> in_vals_for_logic;
            in_vals_for_logic.reserve(ordered_input_netnames.size());
            for (const auto &netname : ordered_input_netnames) {
                auto it = net_logic.find(netname);
                int v = (it != net_logic.end()) ? it->second : 0;
                in_vals_for_logic.push_back(v);
            }
            int outv = evaluate_cell_logic(inst_ro.type, in_vals_for_logic);
            if (!inst_ro.output_net.empty()) net_logic[inst_ro.output_net] = outv;
            saved_logic_value[idx] = outv;

            // --- (B) gather candidate arrivals/transitions/value for selecting controlling input using same ordered lists ---
            std::vector<std::string> in_net_names = ordered_input_netnames;
            std::vector<double> cand_arrs; cand_arrs.reserve(in_net_names.size());
            std::vector<double> cand_trans; cand_trans.reserve(in_net_names.size());
            std::vector<int> in_vals; in_vals.reserve(in_net_names.size());

            for (const auto &netname : in_net_names) {
                double base_arr = 0.0, base_tr = 0.0;
                auto nit = M.nets.find(netname);
                if (nit != M.nets.end()) {
                    base_arr = net_arrival_local[netname];
                    base_tr  = net_tran_local[netname];
                }
                double cand_arr = base_arr;
                std::string drv = (nit != M.nets.end() ? nit->second.driver_instance : std::string());
                if (!(drv == "PI" || drv.empty())) cand_arr = base_arr + wire_delay;
                double cand_tr = base_tr;

                cand_arrs.push_back(cand_arr);
                cand_trans.push_back(cand_tr);

                auto itv = net_logic.find(netname);
                int vv = (itv != net_logic.end()) ? itv->second : 0;
                in_vals.push_back(vv);
            }

            // --- (C) pick input for table Y according to rules (NAND->0, NOR->1; earliest among controlling candidates else latest fallback) ---
            int chosen_idx = -1;
            std::string t;
            for (char c: inst_ro.type) if (!std::isdigit((unsigned char)c)) t.push_back(std::toupper((unsigned char)c));
            int controlling_value_known = -1;
            if (t.find("NAND") != std::string::npos) controlling_value_known = 0;
            else if (t.find("NOR") != std::string::npos) controlling_value_known = 1;

            if (controlling_value_known != -1) {
                for (size_t i = 0; i < in_vals.size(); ++i) {
                    if (in_vals[i] == controlling_value_known) {
                        if (chosen_idx == -1) chosen_idx = (int)i;
                        else {
                            double ai = cand_arrs[i], aj = cand_arrs[chosen_idx];
                            if (ai < aj - 1e-12) chosen_idx = (int)i;
                            else if (std::abs(ai - aj) <= 1e-12) {
                                if (cand_trans[i] < cand_trans[chosen_idx]) chosen_idx = (int)i;
                            }
                        }
                    }
                }
            }
            if (chosen_idx == -1 && !in_vals.empty()) {
                for (size_t i = 0; i < in_vals.size(); ++i) {
                    if (chosen_idx == -1) chosen_idx = (int)i;
                    else {
                        double ai = cand_arrs[i], aj = cand_arrs[chosen_idx];
                        if (ai > aj + 1e-12) chosen_idx = (int)i;
                        else if (std::abs(ai - aj) <= 1e-12) {
                            if (cand_trans[i] > cand_trans[chosen_idx]) chosen_idx = (int)i;
                        }
                    }
                }
            }

            // --- (D) determine input_arrival from chosen input (not max) ---
            double input_arrival = 0.0;
            double chosen_input_transition = 0.0;
            std::string chosen_netname;
            if (chosen_idx >= 0) {
                double cand_a = cand_arrs[chosen_idx];
                if (cand_a == -std::numeric_limits<double>::infinity()) input_arrival = 0.0;
                else input_arrival = cand_a;
                chosen_input_transition = cand_trans[chosen_idx];
                chosen_netname = in_net_names[chosen_idx];
            } else {
                input_arrival = 0.0;
                chosen_input_transition = 0.0;
            }

            // --- (E) table lookup and out_arrival as before ---
            double pd_r = 0.0, pd_f = 0.0, ot_r = 0.0, ot_f = 0.0;
            auto cit2 = R.cells.find(inst_ro.type);
            if (cit2 != R.cells.end()) {
                const CellDef &cd = cit2->second;
                pd_r = table2d_lookup(cd.cell_rise, inst_ro.output_loading, chosen_input_transition);
                ot_r = table2d_lookup(cd.rise_tran, inst_ro.output_loading, chosen_input_transition);
                pd_f = table2d_lookup(cd.cell_fall, inst_ro.output_loading, chosen_input_transition);
                ot_f = table2d_lookup(cd.fall_tran, inst_ro.output_loading, chosen_input_transition);
            }

            double cell_delay = (outv == 1) ? pd_r : pd_f;
            double cell_tran  = (outv == 1) ? ot_r : ot_f;

            double out_arrival = input_arrival + cell_delay;
            if (!inst_ro.output_net.empty()) {
                net_arrival_local[inst_ro.output_net] = out_arrival;
                net_tran_local[inst_ro.output_net]   = cell_tran;
            }

            // --- (F) save for PHASE B ---
            saved_cell_delay[idx] = cell_delay;
            saved_cell_tran[idx]  = cell_tran;
            saved_chosen_net[idx] = chosen_netname;
            saved_pd_r[idx] = pd_r; saved_pd_f[idx] = pd_f; saved_ot_r[idx] = ot_r; saved_ot_f[idx] = ot_f;
        } // end PHASE A

        // PHASE B: prepare gate_info text for this pattern into a string and return it
        std::ostringstream local_out;
        local_out << std::fixed << std::setprecision(6);
        for (auto &pp : order_by_parsed) {
            size_t idx = pp.second;
            if (idx >= Ninst) continue;
            const Instance &inst = M.instances[idx];
            local_out << inst.name << " " << saved_logic_value[idx] << " " << saved_cell_delay[idx] << " " << saved_cell_tran[idx] << "\n";
        }
        local_out << "\n";
        return local_out.str();
    }; // end worker lambda

    // launch all patterns
    for (size_t pidx = 0; pidx < P; ++pidx) {
        futs.emplace_back(std::async(std::launch::async, worker, pidx));
    }

    // collect results and write sequentially in main thread
    std::ofstream gofs(gpath, std::ios::app);
    if (!gofs.is_open()) {
        // cannot open gate_info file: fail gracefully
    } else {
        for (size_t pidx = 0; pidx < P; ++pidx) {
            std::string part = futs[pidx].get(); // wait and get
            gofs << part;
        }
        gofs.close();
    }
}


return true;


}


} // namespace sta
