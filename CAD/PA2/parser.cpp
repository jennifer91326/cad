// parser.cpp
#include "parser.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <queue>
#include <iomanip>
#include <cstdio>
#include <queue>
#include <unordered_set>
#include <iostream>

#include <regex>
#include <string>

namespace sta {

/* ---------- utilities ---------- */

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
// trim helper
static inline std::string trim_copy(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
    return s;
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

/* ---------- lib parsing ---------- */
// remove surrounding quotes and trim
static std::string strip_quotes_trim(const std::string &t) {
    size_t a = 0, b = t.size();
    // trim
    while (a < b && std::isspace((unsigned char)t[a])) ++a;
    while (b > a && std::isspace((unsigned char)t[b-1])) --b;
    if (b <= a) return std::string();
    std::string sub = t.substr(a, b-a);
    // strip matching surrounding quotes "..." or '...'
    if (sub.size() >= 2 && ((sub.front() == '"' && sub.back() == '"') || (sub.front() == '\'' && sub.back() == '\''))) {
        return sub.substr(1, sub.size()-2);
    }
    return sub;
}

// extract library name like: library (name) or library("name")
static std::string extract_lib_name_from_text(const std::string &lib_text) {
    std::string s = remove_comments(lib_text);
    size_t p = s.find("library");
    if (p == std::string::npos) return std::string();
    size_t par = s.find('(', p);
    if (par == std::string::npos) return std::string();
    size_t par2 = s.find(')', par+1);
    if (par2 == std::string::npos) return std::string();
    return strip_quotes_trim(s.substr(par+1, par2-par-1));
}

static std::unordered_map<std::string, CellDef> parse_libfile_text(const std::string &txt){
    std::unordered_map<std::string, CellDef> cells;
    std::string s = remove_comments(txt);

    std::vector<double> idx1, idx2;
    // We'll capture the lu_table_template name(s) and their indices
    std::string lu_table_name; // e.g. delay_table10
    size_t pos = s.find("lu_table_template");
    if(pos != std::string::npos){
        // try to capture the name in parentheses after lu_table_template
        size_t name_par_open = s.find('(', pos);
        if (name_par_open != std::string::npos) {
            size_t name_par_close = s.find(')', name_par_open+1);
            if (name_par_close != std::string::npos) {
                lu_table_name = strip_quotes_trim(s.substr(name_par_open+1, name_par_close-name_par_open-1));
            }
        }

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
                if(p1!=std::string::npos){
                    size_t a = blk.find('(', p1);
                    size_t b = blk.find(')', a);
                    if(a!=std::string::npos && b!=std::string::npos) idx1 = parse_number_list(blk.substr(a+1, b-a-1));
                }
                size_t p2 = blk.find("index_2");
                if(p2!=std::string::npos){
                    size_t a = blk.find('(', p2);
                    size_t b = blk.find(')', a);
                    if(a!=std::string::npos && b!=std::string::npos) idx2 = parse_number_list(blk.substr(a+1, b-a-1));
                }
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
        size_t ppos = 0;
        while(true){
            size_t pinpos = block.find("pin", ppos);
            if(pinpos==std::string::npos) break;
            size_t ppar = block.find('(', pinpos);
            if(ppar==std::string::npos){ ppos = pinpos+3; continue; }
            size_t ppar2 = block.find(')', ppar);
            if(ppar2==std::string::npos){ ppos = pinpos+3; continue; }
            std::string pinname = trim(block.substr(ppar+1, ppar2-ppar-1));

            // record pin name (keep the exact name as in lib)
            cd.pin_list.push_back(pinname);

            // find pin block { ... } as before
            size_t pbr = block.find('{', ppar2);
            if (pbr == std::string::npos){ ppos = ppar2+1; continue; }
            int pdepth=1; size_t j=pbr+1;
            for(; j<block.size(); ++j){ if(block[j]=='{') ++pdepth; else if(block[j]=='}'){ if(--pdepth==0) break; } }
            if(j>=block.size()) break;
            std::string pblk = block.substr(pbr+1, j-pbr-1);

            // parse direction if present (normalize to lower-case)
            size_t dirpos = pblk.find("direction");
            if (dirpos != std::string::npos) {
                size_t colon = pblk.find(':', dirpos);
                if (colon != std::string::npos) {
                    size_t semi = pblk.find(';', colon);
                    std::string dirval = (semi == std::string::npos) ? pblk.substr(colon+1) : pblk.substr(colon+1, semi-colon-1);
                    dirval = trim(dirval);
                    std::string dirnorm;
                    for (char ch : dirval) dirnorm.push_back((char)std::tolower((unsigned char)ch));
                    if (dirnorm == "input" || dirnorm == "output" || dirnorm == "inout") {
                        cd.pin_dir[pinname] = dirnorm;
                        if (dirnorm == "output") cd.output_pins.push_back(pinname);
                    } else {
                        // store what we found (for debug), even if unknown
                        cd.pin_dir[pinname] = dirnorm;
                    }
                }
            }

            // parse capacitance if present (existing logic)
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
        size_t tpos = block.find("timing");
        if(tpos!=std::string::npos){
            size_t tbr = block.find('{', tpos);
            if(tbr!=std::string::npos){
                int depth2 = 1; size_t j=tbr+1;
                for(; j<block.size(); ++j){ if(block[j]=='{') ++depth2; else if(block[j]=='}'){ if(--depth2==0) break; } }
                if(j<block.size()){
                    std::string tblk = block.substr(tbr+1, j-tbr-1);

                    // Helper: parse_values but only accept if the table identifier in parentheses
                    // matches lu_table_name (if lu_table_name not empty). If lu_table_name is empty,
                    // accept any table name.
                    auto parse_values_with_table_check = [&](const std::string &blk, const std::string &field)->Table2D{
                        Table2D tab;
                        tab.idx_cap = idx1; tab.idx_tran = idx2;
                        size_t posf = blk.find(field);
                        if(posf==std::string::npos) return tab;
                        // check following '(' name ')' after the field token
                        size_t par = blk.find('(', posf);
                        if(par == std::string::npos) return tab;
                        size_t par2 = blk.find(')', par+1);
                        if(par2 == std::string::npos) return tab;
                        std::string table_id = strip_quotes_trim(blk.substr(par+1, par2-par-1));
                        // if we have an expected lu_table_name, require match
                        if(!lu_table_name.empty() && table_id != lu_table_name) {
                            // not matching the lu_table_template name -> skip this field
                            return tab;
                        }
                        size_t br2 = blk.find('{', par2);
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

                    cd.cell_rise = parse_values_with_table_check(tblk, "cell_rise");
                    cd.cell_fall = parse_values_with_table_check(tblk, "cell_fall");
                    cd.rise_tran = parse_values_with_table_check(tblk, "rise_transition");
                    cd.fall_tran = parse_values_with_table_check(tblk, "fall_transition");
                }
            }
        }

        

        cells[cellname] = std::move(cd);
        curpos = i+1;
    }

    return cells;
}

/* ---------- netlist parsing ---------- */
static std::string upper_copy(const std::string &s) {
    std::string u = s;
    for (char &c : u) c = (char)std::toupper((unsigned char)c);
    return u;
}
static std::string extract_module_name_from_nettext(const std::string &net_text) {
    std::string cleaned = remove_comments(net_text);
    size_t p = cleaned.find("module");
    if (p == std::string::npos) return std::string();
    size_t i = p + 6;
    while (i < cleaned.size() && std::isspace((unsigned char)cleaned[i])) ++i;
    if (i >= cleaned.size()) return std::string();
    size_t j = i;
    while (j < cleaned.size() && (std::isalnum((unsigned char)cleaned[j]) || cleaned[j]=='_' || cleaned[j]=='$' || cleaned[j]=='.')) ++j;
    if (j > i) return trim(cleaned.substr(i, j-i));
    return std::string();
}

static NetlistModel parse_netlist_text(const std::string &txt){
    NetlistModel M;
    std::string s = remove_comments(txt);

    // extract module name if present
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

    // split into statements by semicolon (simple)
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
            size_t i = 0;
            while(i < l.size()){
                while(i < l.size() && std::isspace((unsigned char)l[i])) ++i;
                if(i >= l.size()) break;
                size_t j = i;
                while(j < l.size() && (std::isalnum((unsigned char)l[j]) || l[j]=='_' )) ++j;
                if(j==i){ ++i; continue; }

                std::string raw_type = l.substr(i, j-i);
                std::string up = upper_copy(raw_type);
                std::string canonical_type = raw_type;

                std::string celltype = raw_type;

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
                    /*std::string s2 = trim(pp);
                    if(s2.empty()) continue;
                    if(s2.front()=='.') s2 = s2.substr(1);
                    size_t a = s2.find('(');
                    size_t b = s2.find(')');
                    if(a == std::string::npos || b == std::string::npos) continue;
                    std::string pin = trim(s2.substr(0,a));
                    std::string net = trim(s2.substr(a+1, b-a-1));
                    size_t comma = net.find(',');
                    if(comma != std::string::npos) net = trim(net.substr(0,comma));

                    // Minimal normalize pin: strip leading '$', strip bus index [..]
                    if(!pin.empty() && (pin.front()=='.' || pin.front()=='$')) pin = pin.substr(1);
                    size_t br_idx = pin.find('[');
                    if (br_idx != std::string::npos) pin = pin.substr(0, br_idx);
                    inst.pin_net[pin] = net;*/
// inside the pair parsing loop, before storing inst.pin_net
std::string raw_pp = pp;                       // original chunk
std::string s2 = trim(pp);


if(!s2.empty() && s2.front()=='.') s2 = s2.substr(1);
size_t a = s2.find('(');
size_t b = s2.find(')');
if(a == std::string::npos || b == std::string::npos) {
   
    continue;
}
std::string pin_raw = trim(s2.substr(0,a));
std::string net = trim(s2.substr(a+1, b-a-1));
size_t comma = net.find(',');
if(comma != std::string::npos) net = trim(net.substr(0,comma));

// minimal normalize (do NOT map names to library pins here)
std::string pin_norm = pin_raw;
if(!pin_norm.empty() && pin_norm.front()=='$') pin_norm = pin_norm.substr(1);
size_t br_idx = pin_norm.find('[');
if(br_idx != std::string::npos) pin_norm = pin_norm.substr(0, br_idx);

inst.pin_net[pin_norm] = net;
                    
                    
                }

                // Skip storing this instance if its name equals the module name
                if(!M.module_name.empty() && inst.name == M.module_name) {
                    i = p2+1;
                    continue;
                }

                M.instances.push_back(std::move(inst));

                i = p2+1;
            }
        }
    }

    // determine driver nets using heuristic (compute_derived will refine using lib)
    for(const auto &inst : M.instances){
        std::string outnet;
        if(inst.pin_net.count("ZN")) outnet = inst.pin_net.at("ZN");
        else if(inst.pin_net.count("Z")) outnet = inst.pin_net.at("Z");
        else {
            for(auto &kv : inst.pin_net){
                std::string pin = kv.first;
                if(pin=="A1"||pin=="A2"||pin=="I"||pin=="A"||pin=="B") continue;
                outnet = kv.second;
                break;
            }
            if(outnet.empty() && !inst.pin_net.empty()) outnet = inst.pin_net.begin()->second;
        }
        if(!outnet.empty()){
            if(!M.nets.count(outnet)) M.nets[outnet] = Net{outnet,"",{} , false};
            M.nets[outnet].driver_instance = inst.name;
        }
        for(auto &kv : inst.pin_net){
            std::string pin = kv.first, net = kv.second;
            bool is_output = (pin=="ZN" || pin=="Z");
            if(!M.nets.count(net)) M.nets[net] = Net{net,"",{}, false};
            if(!is_output) M.nets[net].sinks.push_back(inst.name);
        }
    }

    // primary I/O
    for(auto &pi : M.primary_inputs){
        if(!M.nets.count(pi)) M.nets[pi] = Net{pi,"PI",{}, false};
        else M.nets[pi].driver_instance = "PI";
    }
    for(auto &po : M.primary_outputs){
        if(!M.nets.count(po)) M.nets[po] = Net{po,"",{}, true};
        else M.nets[po].is_primary_output = true;
    }
    return M;
}


/* ---------- pattern parsing ---------- */

static std::vector<std::unordered_map<std::string,int>> parse_pattern_text(const std::string &txt){
    std::vector<std::unordered_map<std::string,int>> patterns;
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
            for(size_t j=i+1;j<lines.size();++j){
                std::string P = trim(lines[j]);
                if(P.empty()) continue;
                if(P.rfind(".end",0)==0) break;
                std::vector<char> bits;
                for(char c: P) if(c=='0'||c=='1') bits.push_back(c);
                if(bits.empty()) continue;
                if(bits.size() != header_inputs.size()){
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
                    std::unordered_map<std::string,int> m;
                    for(size_t k=0;k<bits.size();++k) m[header_inputs[k]] = (bits[k]=='1') ? 1 : 0;
                    patterns.push_back(std::move(m));
                }
            }
            break;
        }
    }
    return patterns;
}


/* ---------- exposed API ---------- */

ParseResult parse_texts(const std::string &net_text,
                        const std::string &lib_text,
                        const std::string &pattern_text,
                        const std::string &net_basename_fallback,
                        const std::string &lib_basename_fallback)
{
    ParseResult R;

    // �q���e�^���A�Y�^�����ѫh�ϥζǤJ�� fallback basename
    std::string lib_name = extract_lib_name_from_text(lib_text);
    // std::cout<<lib_name<<"\n";   // debug output commented out
    std::string module_name = extract_module_name_from_nettext(net_text);
    
    R.lib_basename = lib_name.empty() ? lib_basename_fallback : lib_name;
    R.net_basename = module_name.empty() ? net_basename_fallback : module_name;

    R.cells = parse_libfile_text(lib_text);
    R.netlist = parse_netlist_text(net_text);
    R.input_patterns = parse_pattern_text(pattern_text);
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

/* ---------- compute derived fields: output_net, loading, topo ---------- */

void compute_derived(ParseResult &R){
    auto &M = R.netlist;

    // Ensure Instance has these fields:
    // std::string output_pin;
    // std::string output_net;
    // double output_loading;

    // 1) determine output_net/output_pin for each instance using library pin_dir/pin_list
    for (auto &inst : M.instances) {
        std::string outnet;
        std::string outpin;


        // try lookup of cell definition (allow direct or upper-case fallback)
        auto cit = R.cells.find(inst.type);
        if (cit == R.cells.end()) cit = R.cells.find(upper_copy(inst.type));

        if (cit != R.cells.end()) {
            const CellDef &cd = cit->second;

            // (A) prefer pins explicitly marked as "output" in pin_dir, in pin_list order
            bool found = false;
            if (!cd.pin_dir.empty()) {
                for (const auto &pin : cd.pin_list) {
                    auto dit = cd.pin_dir.find(pin);
                    if (dit != cd.pin_dir.end() && dit->second == "output") {
                        // match this pin name against instance pin names (exact then case-insensitive)
                        auto it = inst.pin_net.find(pin);
                        if (it == inst.pin_net.end()) {
                            for (const auto &kv : inst.pin_net) {
                                if (upper_copy(kv.first) == upper_copy(pin)) { it = inst.pin_net.find(kv.first); break; }
                            }
                        }
                        if (it != inst.pin_net.end()) { outnet = it->second; outpin = pin; found = true; break; }
                    }
                }
            }

            // (B) fallback: look for common output names inside pin_list (preserve lib order)
            if (!found) {
                for (const auto &pin : cd.pin_list) {
                    if (pin == "ZN" || pin == "Z" || pin == "OUT" || pin == "O" || pin == "Y") {
                        auto it = inst.pin_net.find(pin);
                        if (it == inst.pin_net.end()) {
                            for (const auto &kv : inst.pin_net) {
                                if (upper_copy(kv.first) == upper_copy(pin)) { it = inst.pin_net.find(kv.first); break; }
                            }
                        }
                        if (it != inst.pin_net.end()) { outnet = it->second; outpin = pin; found = true; break; }
                    }
                }
            }

            // (C) last resort: any pin from pin_list that exists in instance mapping
            if (!found) {
                for (const auto &pin : cd.pin_list) {
                    auto it = inst.pin_net.find(pin);
                    if (it == inst.pin_net.end()) {
                        for (const auto &kv : inst.pin_net) {
                            if (upper_copy(kv.first) == upper_copy(pin)) { it = inst.pin_net.find(kv.first); break; }
                        }
                    }
                    if (it != inst.pin_net.end()) { outnet = it->second; outpin = pin; found = true; break; }
                }
            }
        }

        // 2) fallback: legacy heuristic if library info not usable
        if (outnet.empty()) {
            if (inst.pin_net.count("ZN")) { outnet = inst.pin_net.at("ZN"); outpin = "ZN"; }
            else if (inst.pin_net.count("Z")) { outnet = inst.pin_net.at("Z"); outpin = "Z"; }
            else {
                for (auto &kv : inst.pin_net) {
                    std::string pin = kv.first;
                    if (pin == "A1" || pin == "A2" || pin == "I" || pin == "A" || pin == "B") continue;
                    outnet = kv.second;
                    outpin = pin;
                    break;
                }
                if (outnet.empty() && !inst.pin_net.empty()) { outnet = inst.pin_net.begin()->second; outpin = inst.pin_net.begin()->first; }
            }
        }

        // store decided output pin/net on instance
        inst.output_pin = outpin;
        inst.output_net = outnet;

        

        // populate net driver entry
        if (!outnet.empty()) {
            if (!M.nets.count(outnet)) M.nets[outnet] = Net{outnet, "", {}, false};
            M.nets[outnet].driver_instance = inst.name;
        }
    } // end for instances

    // 2) ensure primary inputs and primary outputs exist in nets and mark
    for (const auto &pi : M.primary_inputs) {
        if (!M.nets.count(pi)) M.nets[pi] = Net{pi, "PI", {}, false};
        else M.nets[pi].driver_instance = "PI";
    }
    for (const auto &po : M.primary_outputs) {
        if (!M.nets.count(po)) M.nets[po] = Net{po, "", {}, true};
        else M.nets[po].is_primary_output = true;
    }

    // 3) rebuild sinks lists using library pin_dir/pin_list (clear existing sinks first)
    for (auto &kv : M.nets) kv.second.sinks.clear();

    for (const auto &inst : M.instances) {
        // prepare outpins set from lib for fallback (case-insensitive)
        std::unordered_set<std::string> outpins;
        auto cit = R.cells.find(inst.type);
        if (cit == R.cells.end()) cit = R.cells.find(upper_copy(inst.type));
        if (cit != R.cells.end()) {
            const CellDef &cd = cit->second;
            if (!cd.pin_dir.empty()) {
                for (const auto &p : cd.pin_list) {
                    auto dit = cd.pin_dir.find(p);
                    if (dit != cd.pin_dir.end() && dit->second == "output") {
                        outpins.insert(p);
                        outpins.insert(upper_copy(p));
                    }
                }
            }
            // fallback: common names
            if (outpins.empty()) {
                for (const auto &p : cd.pin_list) {
                    if (p == "ZN" || p == "Z" || p == "OUT" || p == "O" || p == "Y") {
                        outpins.insert(p);
                        outpins.insert(upper_copy(p));
                        break;
                    }
                }
            }
        } else {
            // legacy assumption
            outpins.insert("ZN"); outpins.insert("Z");
        }

        // for each pin on instance, if it's not the chosen output pin then it's a sink for that net
        for (const auto &pn : inst.pin_net) {
            const std::string &pin = pn.first;
            const std::string &net = pn.second;
            if (!M.nets.count(net)) M.nets[net] = Net{net, "", {}, false};

            bool is_output_pin = false;
            if (!inst.output_pin.empty()) {
                if (pin == inst.output_pin || upper_copy(pin) == upper_copy(inst.output_pin)) is_output_pin = true;
            } else {
                if (outpins.count(pin) > 0 || outpins.count(upper_copy(pin)) > 0) is_output_pin = true;
            }

            if (!is_output_pin) {
                auto &vec = M.nets[net].sinks;
                if (std::find(vec.begin(), vec.end(), inst.name) == vec.end()) vec.push_back(inst.name);
            } else {
                M.nets[net].driver_instance = inst.name;
            }
        }
    }

    // 4) compute output_loading for each instance (base 0.03 if net is primary output, plus sink pin caps)
    std::unordered_map<std::string, size_t> inst_index;
    inst_index.reserve(M.instances.size());
    for (size_t i = 0; i < M.instances.size(); ++i) inst_index[M.instances[i].name] = i;

    bool dbg_verbose_load = true;
    bool dbg_once_load = true;

    for (auto &inst : M.instances) {
        double load = 0.0;

        if (inst.output_net.empty()) {
            
            inst.output_loading = 0.0;
            continue;
        }

        auto nit = M.nets.find(inst.output_net);
        if (nit == M.nets.end()) {
            
            inst.output_loading = 0.0;
            continue;
        }

        const Net &net = nit->second;
        load = net.is_primary_output ? 0.03 : 0.0;
        

        // accumulate sink pin capacitances
        for (const auto &sink_name : net.sinks) {
            if (sink_name == inst.name) {
                
                continue;
            }
            auto sit = inst_index.find(sink_name);
            if (sit == inst_index.end()) {
                
                continue;
            }
            const Instance &sink_inst = M.instances[sit->second];
            

            // find cell def for sink
            auto scit = R.cells.find(sink_inst.type);
            if (scit == R.cells.end()) scit = R.cells.find(upper_copy(sink_inst.type));
            if (scit == R.cells.end()) {
                
                continue;
            }

            // For this sink instance, find all pins that connect to this driver net and add their caps
            for (const auto &pn : sink_inst.pin_net) {
                if (pn.second != inst.output_net) continue; // only pins tied to this driver net
                const std::string &pin = pn.first;
                double cap = 0.0;
                

                // try exact key then uppercase fallback
                auto pit = scit->second.pin_cap.find(pin);
                if (pit != scit->second.pin_cap.end()) { cap = pit->second;  }
                else {
                    pit = scit->second.pin_cap.find(upper_copy(pin));
                    if (pit != scit->second.pin_cap.end()) { cap = pit->second;  }
                }

                load += cap;

                

                
            } // end sink_inst.pin_net loop
        } // end net.sinks loop

        inst.output_loading = load;
        
    }



    // 5) build adjacency and topo (unchanged)
    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, int> indeg;
    for (const auto &inst : M.instances) indeg[inst.name] = 0;
    for (const auto &kv : M.nets) {
        const Net &net = kv.second;
        const std::string &drv = net.driver_instance;
        if (drv.empty() || drv == "PI") continue;
        for (const auto &s : net.sinks) {
            if (s == drv) continue;
            adj[drv].push_back(s);
            indeg[s] += 1;
        }
    }
    std::queue<std::string> q;
    for (const auto &p : indeg) if (p.second == 0) q.push(p.first);
    int idx = 0;
    for (auto &inst : M.instances) inst.topo_index = -1;
    while (!q.empty()) {
        std::string u = q.front(); q.pop();
        auto it = inst_index.find(u);
        if (it != inst_index.end()) M.instances[it->second].topo_index = idx++;
        auto ait = adj.find(u);
        if (ait != adj.end()) {
            for (const auto &v : ait->second) {
                indeg[v] -= 1;
                if (indeg[v] == 0) q.push(v);
            }
        }
    }
    for (auto &inst : M.instances) if (inst.topo_index == -1) inst.topo_index = idx++;
}




/* ---------- json dump (full) ---------- */

// minimal JSON string escaper
static std::string json_escape(const std::string &s)
{
    std::string out;
    for(char c : s){
        switch(c){
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else out.push_back(c);
        }
    }
    return out;
}

static void write_table2d_json(std::ostream &os, const Table2D &t) {
    os << "{";
    os << "\"idx_cap\":[";
    for(size_t i=0;i<t.idx_cap.size();++i){ if(i) os << ","; os << std::setprecision(12) << t.idx_cap[i]; }
    os << "],";
    os << "\"idx_tran\":[";
    for(size_t i=0;i<t.idx_tran.size();++i){ if(i) os << ","; os << std::setprecision(12) << t.idx_tran[i]; }
    os << "],";
    os << "\"vals\":[";
    for(size_t r=0;r<t.vals.size();++r){
        if(r) os << ",";
        os << "[";
        for(size_t c=0;c<t.vals[r].size();++c){
            if(c) os << ",";
            os << std::setprecision(12) << t.vals[r][c];
        }
        os << "]";
    }
    os << "]";
    os << "}";
}

void write_full_parsed_json(const ParseResult &R, const std::string &outfname) {
    std::ofstream ofs(outfname);
    if(!ofs) return;
    ofs << std::fixed;
    ofs << "{\n";
    ofs << "  \"lib_basename\": \"" << json_escape(R.lib_basename) << "\",\n";
    ofs << "  \"net_basename\": \"" << json_escape(R.net_basename) << "\",\n";
    ofs << "  \"module_name\": \"" << json_escape(R.netlist.module_name) << "\",\n";

    ofs << "  \"primary_inputs\": [";
    for(size_t i=0;i<R.netlist.primary_inputs.size();++i){
        if(i) ofs << ", ";
        ofs << "\"" << json_escape(R.netlist.primary_inputs[i]) << "\"";
    }
    ofs << "],\n";

    ofs << "  \"primary_outputs\": [";
    for(size_t i=0;i<R.netlist.primary_outputs.size();++i){
        if(i) ofs << ", ";
        ofs << "\"" << json_escape(R.netlist.primary_outputs[i]) << "\"";
    }
    ofs << "],\n";

   // cells
    ofs << "  \"cells\": {\n";
    bool firstCell = true;
    for(const auto &cell_kv : R.cells){
        if(!firstCell) ofs << ",\n";
        firstCell = false;
        const std::string &cellname = cell_kv.first;
        const CellDef &cd = cell_kv.second;
        ofs << "    \"" << json_escape(cellname) << "\": {\n";
        ofs << "      \"name\": \"" << json_escape(cd.name) << "\",\n";
        ofs << "      \"pin_cap\": {";
        bool firstPin=true;
        for(const auto &pc : cd.pin_cap){
            if(!firstPin) ofs << ", ";
            firstPin=false;
            ofs << "\"" << json_escape(pc.first) << "\": " << std::setprecision(12) << pc.second;
        }
        ofs << "},\n";
        ofs << "      \"cell_rise\": "; write_table2d_json(ofs, cd.cell_rise); ofs << ",\n";
        ofs << "      \"cell_fall\": "; write_table2d_json(ofs, cd.cell_fall); ofs << ",\n";
        ofs << "      \"rise_tran\": "; write_table2d_json(ofs, cd.rise_tran); ofs << ",\n";
        ofs << "      \"fall_tran\": "; write_table2d_json(ofs, cd.fall_tran); ofs << "\n";
        ofs << "    }";
    }
    ofs << "\n  },\n";

    // instances (including derived fields)
    ofs << "  \"instances\": [\n";
    for(size_t i=0;i<R.netlist.instances.size();++i){
        const auto &ins = R.netlist.instances[i];
        ofs << "    {\n";
        ofs << "      \"name\": \"" << json_escape(ins.name) << "\",\n";
        ofs << "      \"type\": \"" << json_escape(ins.type) << "\",\n";
        ofs << "      \"parsed_index\": " << ins.parsed_index << ",\n";
        ofs << "      \"topo_index\": " << ins.topo_index << ",\n";
        ofs << "      \"output_net\": \"" << json_escape(ins.output_net) << "\",\n";
        ofs << "      \"output_loading\": " << std::setprecision(12) << ins.output_loading << ",\n";
        ofs << "      \"pins\": {\n";
        bool firstPin=true;
        for(auto it = ins.pin_net.begin(); it != ins.pin_net.end(); ++it){
            if(!firstPin) ofs << ",\n";
            firstPin=false;
            ofs << "        \"" << json_escape(it->first) << "\": \"" << json_escape(it->second) << "\"";
        }
        ofs << "\n      }\n";
        ofs << "    }";
        if(i+1 < R.netlist.instances.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n";

    // nets (driver + sinks + primary flag)
    ofs << "  \"nets\": [\n";
    size_t netcount=0;
    for(const auto &net_kv : R.netlist.nets){
        const auto &n = net_kv.second;
        ofs << "    {\n";
        ofs << "      \"name\": \"" << json_escape(n.name) << "\",\n";
        ofs << "      \"driver\": \"" << json_escape(n.driver_instance) << "\",\n";
        ofs << "      \"is_primary_output\": " << (n.is_primary_output ? "true" : "false") << ",\n";
        ofs << "      \"sinks\": [";
        for(size_t i=0;i<n.sinks.size();++i){
            if(i) ofs << ", ";
            ofs << "\"" << json_escape(n.sinks[i]) << "\"";
        }
        ofs << "]\n";
        ofs << "    }";
        ++netcount;
        if(netcount < R.netlist.nets.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n";

    // patterns
    ofs << "  \"patterns\": [\n";
    for(size_t pi=0; pi<R.input_patterns.size(); ++pi){
        const auto &p = R.input_patterns[pi];
        ofs << "    {\n";
        size_t cnt=0;
        for(auto it = p.begin(); it != p.end(); ++it){
            ofs << "      \"" << json_escape(it->first) << "\": \"" << it->second << "\"";
            ++cnt;
            if(cnt < p.size()) ofs << ",\n";
            else ofs << "\n";
        }
        ofs << "    }";
        if(pi+1 < R.input_patterns.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "  ]\n";
    ofs << "}\n";
    ofs.close();
}

} // namespace sta
