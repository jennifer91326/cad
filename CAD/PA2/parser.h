#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <unordered_map>

namespace sta {

struct Table2D {
    std::vector<double> idx_cap;    // row indices (output loading)
    std::vector<double> idx_tran;   // col indices (input transition)
    std::vector<std::vector<double>> vals; // vals[row][col]
};

struct CellDef {
    std::string name;
    std::unordered_map<std::string,double> pin_cap;
    Table2D cell_rise;
    Table2D cell_fall;
    Table2D rise_tran;
    Table2D fall_tran;
};

struct Instance {
    std::string type;
    std::string name;
    std::unordered_map<std::string,std::string> pin_net; // pin -> net
    int parsed_index = -1; // order in netlist
};

struct Net {
    std::string name;
    std::string driver_instance; // instance name or "PI" or empty
    std::vector<std::string> sinks; // instance names
};

struct NetlistModel {
    std::string module_name;
    std::vector<std::string> primary_inputs;
    std::vector<std::string> primary_outputs;
    std::vector<Instance> instances;
    std::unordered_map<std::string, Net> nets; // net name -> Net
};

struct ParseResult {
    std::string lib_basename;
    std::string net_basename;
    std::unordered_map<std::string, CellDef> cells;
    NetlistModel netlist;
    std::vector<std::unordered_map<std::string,char>> patterns;
};

// Parse helpers - file-level functions
// parse lib file contents into CellDef map
ParseResult parse_files(const std::string &netfile_path,
                        const std::string &libfile_path,
                        const std::string &patternfile_path);

// Variant: parse from raw text (useful for tests)
ParseResult parse_texts(const std::string &net_text,
                        const std::string &lib_text,
                        const std::string &pattern_text,
                        const std::string &net_basename = "net",
                        const std::string &lib_basename = "lib");

} // namespace sta

#endif // PARSER_H
