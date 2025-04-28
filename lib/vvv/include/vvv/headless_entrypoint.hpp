//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <string>

int entrypoint_main(int (*main)(int, char **), int argc, char **argv, const std::string &dataDirs);

#define ENTRYPOINT(SUBROUTINE)                                     \
    int main(int argc, char *argv[]) {                             \
        return entrypoint_main(SUBROUTINE, argc, argv, DATA_DIRS); \
    }
