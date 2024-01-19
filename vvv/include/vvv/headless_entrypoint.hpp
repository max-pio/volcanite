#pragma once

// ToDo: the headless_entrypoint.hpp variant of the vvv library is a copy+paste version of vvvwindow/entrypoint._pp

#include <string>

int entrypoint_main(int(*main)(int, char**), int argc, char **argv, const std::string& dataDirs);

#define ENTRYPOINT(SUBROUTINE)\
    int main(int argc, char *argv[]) {\
        return entrypoint_main(SUBROUTINE, argc, argv, DATA_DIRS);\
    }

