#include "parser.h"
#include <fstream>
#include <iostream>

// 簡單示範如何呼叫 parser 並把結果序列化為 JSON（便於檢查）
static void write_parsed_json(const sta::ParseResult &R, const std::string &outfname){
    std::ofstream ofs(outfname);
    ofs << "{\n";
    ofs << "\"lib_basename\":\"" << R.lib_basename << "\",\n";
    ofs << "\"net_basename\":\"" << R.net_basename << "\",\n";
    ofs << "\"module\":\"" << R.netlist.module_name << "\",\n";
    ofs << "\"primary_inputs\":[";
    for(size_t i=0;i<R.netlist.primary_inputs.size();++i){ if(i) ofs<<","; ofs<<"\""<<R.netlist.primary_inputs[i]<<"\""; }
    ofs << "],\n";
    ofs << "\"primary_outputs\":[";
    for(size_t i=0;i<R.netlist.primary_outputs.size();++i){ if(i) ofs<<","; ofs<<"\""<<R.netlist.primary_outputs[i]<<"\""; }
    ofs << "],\n";
    ofs << "\"instances\":[\n";
    for(size_t i=0;i<R.netlist.instances.size();++i){
        const auto &ins = R.netlist.instances[i];
        ofs << " {\"name\":\""<<ins.name<<"\",\"type\":\""<<ins.type<<"\",\"index\":"<<ins.parsed_index<<",\"pins\":{";
        bool f=true;
        for(auto &kv: ins.pin_net){ if(!f) ofs<<","; f=false; ofs<<"\""<<kv.first<<"\":\""<<kv.second<<"\""; }
        ofs << "}}";
        if(i+1<R.netlist.instances.size()) ofs << ",\n";
    }
    ofs << "],\n";
    ofs << "\"cells\":[\n";
    bool first=true;
    for(auto &kv: R.cells){
        if(!first) ofs << ",\n"; first=false;
        ofs << " {\"cell\":\""<<kv.first<<"\",\"pins\":{";
        bool f=true;
        for(auto &p: kv.second.pin_cap){ if(!f) ofs<<","; f=false; ofs<<"\""<<p.first<<"\":"<<p.second; }
        ofs << "}}";
    }
    ofs << "]\n";
    ofs << "}\n";
}

int main(int argc, char** argv){
    if(argc < 6) return 1;
    std::string netf = argv[1];
    std::string libf, patf;
    for(int i=2;i<argc;i++){
        std::string a = argv[i];
        if(a=="-l" && i+1<argc) { libf = argv[++i]; }
        else if(a=="-i" && i+1<argc) { patf = argv[++i]; }
    }
    auto res = sta::parse_files(netf, libf, patf);
    std::string out = res.lib_basename + "_" + res.net_basename + "_parsed.json";
    write_parsed_json(res, out);
    return 0;
}
