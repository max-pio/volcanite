#pragma once

#include <string>

int entrypoint_main(int(*main)(int, char**), int argc, char **argv, const std::string& dataDirs);

#define ENTRYPOINT(SUBROUTINE)\
    int main(int argc, char *argv[]) {\
        entrypoint_main(SUBROUTINE, argc, argv, DATA_DIRS);\
    }

