#ifndef SLATE_COMPLETION_H
#define SLATE_COMPLETION_H

#include <string>
#include "CLI11.hpp"

void getCompletionScript(const CLI::App& cmd, std::string shell);

#endif //SLATE_COMPLETION_H
