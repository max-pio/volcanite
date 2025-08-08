//  Copyright (C) 2024, Max Piochowiak and Fabian Schiekel, Karlsruhe Institute of Technology
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

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

/// @returns the Levenshtein distance between s1 and s2.
int levenshtein_distance(const std::string_view s1, const std::string_view s2) {
    size_t len1 = s1.size(), len2 = s2.size();
    std::vector<std::vector<int>> dp(len1 + 1, std::vector<int>(len2 + 1));
    for (size_t i = 0; i <= len1; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= len2; ++j) dp[0][j] = j;
    for (size_t i = 1; i <= len1; ++i)
        for (size_t j = 1; j <= len2; ++j)
            dp[i][j] = std::min({ dp[i-1][j] + 1, dp[i][j-1] + 1,
                                dp[i-1][j-1] + (s1[i-1] != s2[j-1]) });
    return dp[len1][len2];
}


/// Checks if target is in list and returns a possible closest match in list. If target is not in list, closest_idx
///  will be set to the closest element in list if its levenstein distance to target is at most close_distance.
/// Otherwise, closest_idx will be set to -1.
/// @param closest_idx set to: index of target in list, or of its closest match if its distance is < close_distance, otherwise list.size().
/// @returns true if target is in list, false otherwise.
bool stringCheckAndSuggest(const std::string_view target, const std::vector<std::string>& list, size_t& closest_idx, const int close_distance = 8) {

    // Exact match
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i] == target) {
            closest_idx = i;
            return true;
        }
    }

    // Find closest
    int min_distance = __INT_MAX__;
    size_t closest = list.size();
    for (size_t i = 0; i < list.size(); i++) {
        int dist = levenshtein_distance(target, list[i]);
        if (dist < min_distance) {
            min_distance = dist;
            closest = i;
        }
    }

    // Optionally, set a distance threshold
    if (min_distance != __INT_MAX__ && min_distance <= close_distance) {
        closest_idx = closest;
    } else {
        closest_idx = list.size();
    }
    return false;
}