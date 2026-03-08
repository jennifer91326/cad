#ifndef STEP1_H
#define STEP1_H

#include "parser.h"
#include <string>

namespace sta {

// write the load file for Step1
// The output filename will be: <lib_basename>_<net_basename>_load.txt
// Returns true on success, false on failure.
bool write_step1_load_file(const ParseResult &R, const std::string &out_dir = ".");
static int name_to_int(const std::string &s);
} // namespace sta

#endif // STEP1_H
