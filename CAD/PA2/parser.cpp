#include "parser.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace sta {

/* -------------------- utilities -------------------- */

static std::string read_file(const std::string &path){
    std::ifstream ifs(path);
    if(!ifs) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::string basename_noext(const std::string &p){
    size_t pos = p.find_last_of("/\\");
    std::string b = (pos==std::string::npos)? p : p.substr(pos+1);
    size_t dot = b.find_last_of('.');
    return dot==std::string::npos? b : b.substr(0,dot);
}

static std::string remove_comments(const std::string &s){
    std::string out; out.reserve(s.size());
    bool in_line=false, in_block=false;
    for(size_t i=0;i<s.size();++i){
        if(!in_line && !in_block && i+1<s.size() && s[i]=='/' && s[i+1]=='/'){ in_line=true; ++i; continue; }
        if(!in_line && !in_block && i+1<s.size() && s[i]=='/' && s[i+1]=='*'){ in_block=true; ++i; continue; }
        if(in_line){
            if(s[i]=='\n'){ in_line=false; out.push_back('\n'); }
            continue;
        }
        if(in_block){
            if(i+1<s.size() && s[i]=='*' && s[i+1]=='/'){ in_block=false; ++i; }
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

static inline std::string trim(const std::string &s){
    size_t a=0,b=s.size();
    while(a<b && std::isspace((unsigned char)s[a])) ++a;
    while(b>a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a,b-a);
}

static std::vector<std::string> split_commas_top(const std::string &s){
    std::vector<std::string> res;
    std::string cur;
    int depth=0;
    for(char c: s){
        if(c=='(') ++depth;
        else if(c==')') --depth;
        if(c==',' && depth==0){ if(!trim(cur).empty()) res.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if(!trim(cur).empty()) res.push_back(trim(cur));
    return res;
}

static std::vector<double> parse_number_list(const std::string &s){
    std::string t = s;
    if(!t.empty() && (t.front()=='"' || t.front()=='\'')) t.erase(t.begin());
    if(!t.empty() && (t.back()=='"' || t.back()=='\'')) t.pop_back();
    std::vector<double> out;
    std::string cur;
    for(char c: t){
        if(c==',' || std::isspace((unsigned char)c)){
            if(!cur.empty()){ try{ out.push_back(std::stod(cur)); }catch(...){} cur.clear(); }
        } else cur.push_back(c);
    }
    if(!cur.empty()){ try{ out.push_back(std::stod(cur)); }catch(...){} }
    return out;
}

/* -------------------- lib parsing -------------------- */

static std::unordered_map<std::string, CellDef> parse_libfile_text(const std::string &txt){
    std::unordered_map<std::string, CellDef> cells;
    std::string s = remove_comments(txt);
    // extract lu_table_template indices if present
    std::vector<double> idx1, idx2;
    size_t pos = s.find("lu_table_template");
    if(pos != std::string::npos){
        size_t br = s.find('{', pos);
        if(br != std::string::npos){
            int depth = 1; size_t i = br+1;
            for(; i<s.size(); ++i){
                if(s[i]=='{') ++depth;
                else if(s[i]=='}'){ if(--depth==0) break; }
            }
            if(i < s.size()){
                std::string blk = s.substr(br+1, i-br-1);
                size_t p1 = blk.find("index_1");
                if(p1!=std::string::npos){ size_t a = blk.find('(', p1); size_t b = blk.find(')', a); if(a!=std::string::npos && b!=std::string::npos) idx1 = parse_number_list(blk.substr(a+1, b-a-1)); }
                size_t p2 = blk.find("index_2");
                if(p2!=std::string::npos){ size_t a = blk.find('(', p2); size_t b = blk.find(')', a); if(a!=std::string::npos && b!=std::string::npos) idx2 = parse_number_list(blk.substr(a+1, b-a-1)); }
            }
        }
    }

    size_t curpos = 0;
    while(true){
        size_t p = s.find("cell", curpos);
        if(p==std::string::npos) break;
        size_t par = s.find('(', p);
        if(par==std::string::npos){ curpos = p+4; continue; }
        size_t par2 = s.find(')', par);
        if(par2==std::string::npos){ curpos = p+4; continue; }
        std::string cellname = trim(s.substr(par+1, par2-par-1));
        size_t br = s.find('{', par2);
        if(br==std::string::npos){ curpos = par2+1; continue; }
        int depth = 1; size_t i = br+1;
        for(; i<s.size(); ++i){
            if(s[i]=='{') ++depth;
            else if(s[i]=='}'){ if(--depth==0) break; }
        }
        if(i>=s.size()) break;
        std::string block = s.substr(br+1, i-br-1);
        CellDef cd; cd.name = cellname;
        // parse pin blocks
        size_t ppos = 0;
        while(true){
            size_t pinpos = block.find("pin", ppos);
            if(pinpos==std::string::npos) break;
            size_t ppar = block.find('(', pinpos);
            if(ppar==std::string::npos){ ppos = pinpos+3; continue; }
            size_t ppar2 = block.find(')', ppar);
            if(ppar2==std::string::npos){ ppos = pinpos+3; continue; }
            std::string pinname = trim(block.substr(ppar+1, ppar2-ppar-1));
            size_t pbr = block.find('{', ppar2);
            if(pbr==std::string::npos){ ppos = ppar2+1; continue; }
            int pdepth=1; size_t j=pbr+1;
            for(; j<block.size(); ++j){ if(block[j]=='{') ++pdepth; else if(block[j]=='}'){ if(--pdepth==0) break; } }
            if(j>=block.size()) break;
            std::string pblk = block.substr(pbr+1, j-pbr-1);
            size_t cappos = pblk.find("capacitance");
            if(cappos!=std::string::npos){
                size_t colon = pblk.find(':', cappos);
                if(colon!=std::string::npos){
                    size_t semi = pblk.find(';', colon);
                    std::string num = (semi==std::string::npos)? pblk.substr(colon+1) : pblk.substr(colon+1, semi-colon-1);
                    num = trim(num);
                    try{ double v = std::stod(num); cd.pin_cap[pinname] = v; } catch(...) {}
                }
            }
            ppos = j+1;
        }
        // parse timing() block for values
        size_t tpos = block.find("timing");
        if(tpos!=std::string::npos){
            size_t tbr = block.find('{', tpos);
            if(tbr!=std::string::npos){
                int depth2 = 1; size_t j=tbr+1;
                for(; j<block.size(); ++j){ if(block[j]=='{') ++depth2; else if(block[j]=='}'){ if(--depth2==0) break; } }
                if(j<block.size()){
                    std::string tblk = block.substr(tbr+1, j-tbr-1);
                    auto parse_values = [&](const std::string &blk, const std::string &field)->Table2D{
                        Table2D tab;
                        tab.idx_cap = idx1; tab.idx_tran = idx2;
                        size_t posf = blk.find(field);
                        if(posf==std::string::npos) return tab;
                        size_t par = blk.find('(', posf);
                        if(par==std::string::npos) return tab;
                        size_t br2 = blk.find('{', par);
                        if(br2==std::string::npos) return tab;
                        int depth3 = 1; size_t k = br2+1;
                        for(; k<blk.size(); ++k){ if(blk[k]=='{') ++depth3; else if(blk[k]=='}'){ if(--depth3==0) break; } }
                        if(k>=blk.size()) return tab;
                        std::string valblk = blk.substr(br2+1, k-br2-1);
                        size_t vpos = valblk.find("values");
                        if(vpos==std::string::npos) return tab;
                        size_t vp = valblk.find('(', vpos);
                        if(vp==std::string::npos) return tab;
                        size_t cp = valblk.find(')', vp);
                        if(cp==std::string::npos) return tab;
                        std::string inner = valblk.substr(vp+1, cp-vp-1);
                        // extract quoted rows
                        std::vector<std::string> rows;
                        std::string cur;
                        bool inquote = false;
                        for(char ch : inner){
                            if(ch=='"'){ inquote = !inquote; if(!inquote){ rows.push_back(cur); cur.clear(); } continue; }
                            if(inquote) cur.push_back(ch);
                        }
                        for(auto &r : rows){
                            tab.vals.emplace_back(parse_number_list(r));
                        }
                        return tab;
                    };
                    cd.cell_rise = parse_values(tblk, "cell_rise");
                    cd.cell_fall = parse_values(tblk, "cell_fall");
                    cd.rise_tran = parse_values(tblk, "rise_transition");
                    cd.fall_tran = parse_values(tblk, "fall_transition");
                }
            }
        }
        cells[cellname] = std::move(cd);
        curpos = i+1;
    }
    return cells;
}

/* -------------------- netlist parsing -------------------- */

static NetlistModel parse_netlist_text(const std::string &txt){
    NetlistModel M;
    std::string s = remove_comments(txt);
    // find module name
    size_t mpos = s.find("module");
    if(mpos != std::string::npos){
        size_t p = s.find('(', mpos);
        if(p != std::string::npos){
            size_t a = mpos + 6;
            while(a < s.size() && std::isspace((unsigned char)s[a])) ++a;
            size_t b = a;
            while(b < s.size() && (std::isalnum((unsigned char)s[b]) || s[b]=='_' )) ++b;
            M.module_name = s.substr(a, b-a);
        }
    }
    // split to semicolon-terminated statements
    std::vector<std::string> statements;
    std::string cur;
    for(char c: s){
        cur.push_back(c);
        if(c==';'){ statements.push_back(cur); cur.clear(); }
    }
    if(!cur.empty()) statements.push_back(cur);
    int inst_counter = 0;
    for(auto &st : statements){
        std::string t = trim(st);
        if(t.empty()) continue;
        std::string l = t;
        while(!l.empty() && std::isspace((unsigned char)l.front())) l.erase(l.begin());
        if(l.rfind("input",0)==0){
            std::string rest = l.substr(5);
            if(!rest.empty() && rest.back()==';') rest.pop_back();
            std::vector<std::string> parts = split_commas_top(rest);
            for(auto &p : parts){
                std::string q = trim(p);
                if(q.empty()) continue;
                // split by commas/whitespace
                std::string tmp;
                for(char c: q){
                    if(c==','){ if(!tmp.empty()){ M.primary_inputs.push_back(trim(tmp)); tmp.clear(); } }
                    else tmp.push_back(c);
                }
                if(!tmp.empty()) M.primary_inputs.push_back(trim(tmp));
            }
            continue;
        } else if(l.rfind("output",0)==0){
            std::string rest = l.substr(6);
            if(!rest.empty() && rest.back()==';') rest.pop_back();
            std::vector<std::string> parts = split_commas_top(rest);
            for(auto &p : parts){
                std::string q = trim(p);
                if(q.empty()) continue;
                std::string tmp;
                for(char c: q){
                    if(c==','){ if(!tmp.empty()){ M.primary_outputs.push_back(trim(tmp)); tmp.clear(); } }
                    else tmp.push_back(c);
                }
                if(!tmp.empty()) M.primary_outputs.push_back(trim(tmp));
            }
            continue;
        } else if(l.rfind("endmodule",0)==0){
            continue;
        } else {
            // instance(s) in this semicolon-block
            size_t i = 0;
            while(i < l.size()){
                while(i < l.size() && std::isspace((unsigned char)l[i])) ++i;
                if(i >= l.size()) break;
                size_t j = i;
                while(j < l.size() && (std::isalnum((unsigned char)l[j]) || l[j]=='_' )) ++j;
                if(j==i){ ++i; continue; }
                std::string celltype = l.substr(i, j-i);
                size_t k = j;
                while(k < l.size() && std::isspace((unsigned char)l[k])) ++k;
                size_t k2 = k;
                while(k2 < l.size() && (std::isalnum((unsigned char)l[k2]) || l[k2]=='_' )) ++k2;
                if(k2==k){ i = j+1; continue; }
                std::string instname = l.substr(k, k2-k);
                size_t par = l.find('(', k2);
                if(par == std::string::npos){ i = k2; continue; }
                int depth = 1; size_t p2 = par+1;
                for(; p2 < l.size(); ++p2){
                    if(l[p2]=='(') ++depth;
                    else if(l[p2]==')'){ if(--depth==0) break; }
                }
                if(p2 >= l.size()){ i = k2; continue; }
                std::string arg = l.substr(par+1, p2-par-1);
                Instance inst;
                inst.type = celltype;
                inst.name = instname;
                inst.parsed_index = inst_counter++;
                auto pairs = split_commas_top(arg);
                for(auto &pp : pairs){
                    std::string s2 = trim(pp);
                    if(s2.empty()) continue;
                    if(s2.front()=='.') s2 = s2.substr(1);
                    size_t a = s2.find('(');
                    size_t b = s2.find(')');
                    if(a == std::string::npos || b == std::string::npos) continue;
                    std::string pin = trim(s2.substr(0,a));
                    std::string net = trim(s2.substr(a+1, b-a-1));
                    size_t comma = net.find(',');
                    if(comma != std::string::npos) net = trim(net.substr(0,comma));
                    inst.pin_net[pin] = net;
                }
                M.instances.push_back(std::move(inst));
                i = p2+1;
            }
        }
    }

    // build nets (driver and sinks)
    for(const auto &inst : M.instances){
        std::string outnet;
        if(inst.pin_net.count("ZN")) outnet = inst.pin_net.at("ZN");
        else if(inst.pin_net.count("Z")) outnet = inst.pin_net.at("Z");
        else {
            for(auto &kv : inst.pin_net){ std::string pin = kv.first; if(pin=="A1"||pin=="A2"||pin=="I"||pin=="A"||pin=="B") continue; outnet = kv.second; break; }
            if(outnet.empty() && !inst.pin_net.empty()) outnet = inst.pin_net.begin()->second;
        }
        if(!outnet.empty()){
            if(!M.nets.count(outnet)) M.nets[outnet] = Net{outnet,"",{}};
            M.nets[outnet].driver_instance = inst.name;
        }
        for(auto &kv : inst.pin_net){
            std::string pin = kv.first, net = kv.second;
            bool is_output = (pin=="ZN" || pin=="Z");
            if(!M.nets.count(net)) M.nets[net] = Net{net,"",{}};
            if(!is_output) M.nets[net].sinks.push_back(inst.name);
        }
    }
    // primary inputs: mark driver as "PI"
    for(auto &pi : M.primary_inputs){
        if(!M.nets.count(pi)) M.nets[pi] = Net{pi,"PI",{}};
        else M.nets[pi].driver_instance = "PI";
    }
    // ensure outputs exist
    for(auto &po : M.primary_outputs){
        if(!M.nets.count(po)) M.nets[po] = Net{po,"",{}};
    }

    return M;
}

/* -------------------- pattern parsing -------------------- */

static std::vector<std::unordered_map<std::string,char>> parse_pattern_text(const std::string &txt){
    std::vector<std::unordered_map<std::string,char>> patterns;
    std::string s = remove_comments(txt);
    std::vector<std::string> lines;
    std::string cur;
    for(char c: s){
        if(c=='\r') continue;
        if(c=='\n'){ lines.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if(!trim(cur).empty()) lines.push_back(trim(cur));
    std::vector<std::string> header_inputs;
    for(size_t i=0;i<lines.size();++i){
        std::string L = trim(lines[i]);
        if(L.empty()) continue;
        if(L.rfind("input",0)==0){
            std::string rest = L.substr(5);
            std::vector<std::string> tokens;
            std::string tmp;
            for(char c: rest){
                if(c==',' || std::isspace((unsigned char)c)){
                    if(!tmp.empty()){ tokens.push_back(tmp); tmp.clear(); }
                } else tmp.push_back(c);
            }
            if(!tmp.empty()) tokens.push_back(tmp);
            for(auto &t: tokens) header_inputs.push_back(t);
            // read subsequent pattern lines
            for(size_t j=i+1;j<lines.size();++j){
                std::string P = trim(lines[j]);
                if(P.empty()) continue;
                if(P.rfind(".end",0)==0) break;
                std::vector<char> bits;
                for(char c: P) if(c=='0'||c=='1') bits.push_back(c);
                if(bits.empty()) continue;
                if(bits.size() != header_inputs.size()){
                    // split tokens
                    std::vector<std::string> toks;
                    std::string tmp2;
                    for(char c: P){
                        if(std::isspace((unsigned char)c)){ if(!tmp2.empty()){ toks.push_back(tmp2); tmp2.clear(); } }
                        else tmp2.push_back(c);
                    }
                    if(!tmp2.empty()) toks.push_back(tmp2);
                    bits.clear();
                    for(auto &tk: toks){
                        for(char c: tk) if(c=='0'||c=='1'){ bits.push_back(c); break; }
                    }
                }
                if(bits.size() == header_inputs.size()){
                    std::unordered_map<std::string,char> m;
                    for(size_t k=0;k<bits.size();++k) m[header_inputs[k]] = bits[k];
                    patterns.push_back(std::move(m));
                }
            }
            break;
        }
    }
    return patterns;
}

/* -------------------- exposed API -------------------- */

ParseResult parse_texts(const std::string &net_text,
                        const std::string &lib_text,
                        const std::string &pattern_text,
                        const std::string &net_basename,
                        const std::string &lib_basename)
{
    ParseResult R;
    R.lib_basename = lib_basename;
    R.net_basename = net_basename;
    R.cells = parse_libfile_text(lib_text);
    R.netlist = parse_netlist_text(net_text);
    R.patterns = parse_pattern_text(pattern_text);
    return R;
}

ParseResult parse_files(const std::string &netfile_path,
                        const std::string &libfile_path,
                        const std::string &patternfile_path)
{
    std::string nettxt = read_file(netfile_path);
    std::string libtxt = read_file(libfile_path);
    std::string pattxt = patternfile_path.empty() ? std::string() : read_file(patternfile_path);
    return parse_texts(nettxt, libtxt, pattxt, basename_noext(netfile_path), basename_noext(libfile_path));
}

} // namespace sta
