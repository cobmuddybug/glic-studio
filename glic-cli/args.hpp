#pragma once

#include <glic/config.hpp>
#include <glic/effects.hpp>

#include <string>

namespace glic {

// Upstream glic-cpp's argument parser, unchanged. argv[1] is the command,
// argv[2] input, argv[3] output, then options. Returns false on --help or too
// few arguments.
bool parseArgs(int argc, char* argv[], std::string& command, std::string& input,
               std::string& output, CodecConfig& config, PostEffectsConfig& postEffects);

} // namespace glic
