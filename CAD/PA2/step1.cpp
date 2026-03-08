// step1.cpp
#include "parser.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <climits>
#include <string>
#include <iostream>

namespace sta {

// helper: extract integer from name (assumes exactly one positive integer present)
static int name_to_int(const std::string &s) {
    std::string num;
    num.reserve(8);
    for (char c : s) {
        if (std::isdigit((unsigned char)c)) num.push_back(c);
    }
    if (num.empty()) return INT_MAX;
    try {
        return std::stoi(num);
    } catch (...) {
        return INT_MAX;
    }
}

// write the load file for Step1
// The output filename will be: <lib_basename>_<net_basename>_load.txt
// Returns true on success, false on failure.
bool write_step1_load_file(const ParseResult &R, const std::string &out_dir = ".") {
    std::string libb = R.lib_basename.empty() ? "lib" : R.lib_basename;
    std::string netb = R.net_basename.empty() ? "net" : R.net_basename;
    std::string fname = libb + "_" + netb + "_load.txt";

    std::string outfname;
#if defined(__cpp_lib_filesystem)
    std::filesystem::path outpath = std::filesystem::path(out_dir) / fname;
    outfname = outpath.string();
#else
    outfname = out_dir.empty() ? fname : (out_dir + "/" + fname);
#endif

    // sanity check: ensure compute_derived was likely run
    bool has_valid_loading = false;
    for (const auto &inst : R.netlist.instances) {
        if (inst.output_loading >= 0.0) { has_valid_loading = true; break; }
    }
    if (!has_valid_loading) {
        // caller should run compute_derived first; return false to indicate missing derived data
        return false;
    }

    std::ofstream ofs(outfname);
    if (!ofs.is_open()) return false;

    // gather (instance_name, loading), skipping any instance whose name equals the module name
    std::vector<std::pair<std::string,double>> entries;
    entries.reserve(R.netlist.instances.size());
    const std::string module_name = R.netlist.module_name;

    for (const auto &inst : R.netlist.instances) {
        if (!module_name.empty() && inst.name == module_name) continue;

        // Use compute_derived result (do not recompute here)
        double load = inst.output_loading;
        if (load < 0.0) load = 0.0; // fallback if some instance missing value
        entries.emplace_back(inst.name, load);
    }

    // sort by integer parsed from gate name (ascending); tie-break by full name
    std::sort(entries.begin(), entries.end(),
        [](const std::pair<std::string,double>& a, const std::pair<std::string,double>& b){
            int ia = name_to_int(a.first);
            int ib = name_to_int(b.first);
            if (ia != ib) return ia < ib;
            return a.first < b.first;
        });

    ofs.setf(std::ios::fixed);
    ofs << std::setprecision(6);
    for (const auto &p : entries) {
        ofs << p.first << " " << p.second << "\n";
    }
    ofs.close();
    return true;
}

} // namespace sta
