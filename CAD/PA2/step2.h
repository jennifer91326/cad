#ifndef STEP2_H
#define STEP2_H

#include "parser.h"
#include <string>

namespace sta {

// Run step2: compute propagation delay and output transition for every instance,
// write results back into ParseResult (instance.prop_delay, instance.output_transition, instance.worst_output),
// and also write a text file: <lib_basename>_<net_basename>_timing.txt
// Each line: <Instance Name> <Worst-case Output> <Propagation Delay> <Output Transition Time>
// Returns true on success.
//bool run_step2_and_write(ParseResult &R, double wire_delay = 0.005, const std::string &out_dir = ".");
//bool run_step2_and_write(ParseResult &R, double wire_delay, const std::string &out_dir, bool debug);
bool run_step2and3(ParseResult &R, double wire_delay, const std::string &out_dir, bool debug);

} // namespace sta

#endif // STEP2_H
