#include "parser.h"
#include "step1.h"
#include "step2.h"
#include <iostream>

int main(int argc, char** argv){
    
    // expected usage: ./sta <netlist_file> -l <lib_file> -i <pattern_file>
    if(argc < 4) return 1;
    
    std::string netf = argv[1];
    std::string libf, patf;
    for(int i=2;i<argc;i++){
        std::string a = argv[i];
        if(a == "-l" && i+1 < argc){ libf = argv[++i]; }
        else if(a == "-i" && i+1 < argc){ patf = argv[++i]; }
    }
    if(libf.empty()) {return 2;}
    

    // parse files
    auto res = sta::parse_files(netf, libf, patf);

    // compute derived fields (output_net, output_loading, topo_index)
    sta::compute_derived(res);
    //std::string out = res.lib_basename + "_" + res.net_basename + "_full_parsed.json";
    

    
    bool ok = sta::write_step1_load_file(res);
    if(!ok) return 3;
    ok = sta::run_step2and3(res, 0.005, ".", true);
    if(!ok) return 3;
    
    //sta::write_full_parsed_json(res, out);
    
    

    return 0;
}

