#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <unordered_map>

namespace sta {

struct Table2D {
    std::vector<double> idx_cap;
    std::vector<double> idx_tran;
    std::vector<std::vector<double>> vals;
};

struct CellDef {
    std::string name;
    std::unordered_map<std::string,double> pin_cap;
    std::unordered_map<std::string,std::string> pin_dir;
    std::vector<std::string> pin_list;
    std::vector<std::string> output_pins;
    Table2D cell_rise;
    Table2D cell_fall;
    Table2D rise_tran;
    Table2D fall_tran;
};

struct Instance {
    std::string type;
    std::string name;
    std::unordered_map<std::string,std::string> pin_net; // pin -> net
    int parsed_index = -1;

    // derived
    std::string output_net;
    std::string output_pin;    // computed output net name (empty if none)
    double output_loading = -1; // sum of fanout input caps; primary output => base 0.03 added by compute_derived
    int topo_index = -1;       // order index from topo sort (-1 if not set)
    double pd_rise = 0.0;
    double pd_fall = 0.0;
    double ot_rise = 0.0;
    double ot_fall = 0.0;
    
    
    // Step2 results (new)
    double prop_delay = 0.0;         // worst-case propagation delay (ns)
    double output_transition = 0.0;  // output transition time (ns) corresponding to worst-case
    char worst_output = '1'; 
    
    bool logic_value = 0; 
    std::string chosen_input_net;  
    std::string pred_instance;
};


struct Net {
    std::string name;
    std::string driver_instance; // instance name or "PI" or empty
    std::vector<std::string> sinks; // instance names
    bool is_primary_output = false;
    
    double arrival_time = 0.0;
    double driven_transition = 0.0;
    bool reachable = false;
    std::string pred_net="";
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
    std::vector<std::unordered_map<std::string,int>> input_patterns;
    
};
//std::vector<std::unordered_map<std::string,char>> patterns;
ParseResult parse_texts(const std::string &net_text,
                        const std::string &lib_text,
                        const std::string &pattern_text,
                        const std::string &net_basename = "net",
                        const std::string &lib_basename = "lib");

ParseResult parse_files(const std::string &netfile_path,
                        const std::string &libfile_path,
                        const std::string &patternfile_path);

void compute_derived(ParseResult &R); // compute output_net, loadings, topo order, mark primary outputs

void write_full_parsed_json(const ParseResult &R, const std::string &outfname);

} // namespace sta

#endif // PARSER_H
