#ifndef SLATE_SECRETLOADING_H
#define SLATE_SECRETLOADING_H

#include <string>
#include <vector>

void parseFromFileSecretEntry(const std::string& arg, std::vector<std::string>& output);
void parseFromDirectorySecretEntry(const std::string& arg, std::vector<std::string>& output);
void parseFromEnvFileSecretEntry(const std::string& arg, std::vector<std::string>& output);

#endif //SLATE_SECRETLOADING_H