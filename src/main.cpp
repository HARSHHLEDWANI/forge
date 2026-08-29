#include <iostream>
#include <string>
#include <vector>

#include "cli/cli.hpp"

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return forge::cli::run(args, std::cout, std::cerr);
}

