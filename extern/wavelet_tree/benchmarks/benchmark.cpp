/*******************************************************************************
 * pasta/wavelet_tree/benchmark/benchmark.cpp
 *
 * Copyright (C) 2022 Florian Kurpicz <florian@kurpicz.org>
 *
 * pasta::wavelet_tree is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pasta::wavelet_tree is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pasta::wavelet_tree.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <thread>

#include <tlx/cmdline_parser.hpp>

#include <pasta/utils/benchmark/do_not_optimize.hpp>
#include <pasta/utils/reduce_alphabet.hpp>
#include <pasta/wavelet_tree/wavelet_tree.hpp>

#include <pasta/utils/benchmark/memory_monitor.hpp>

using namespace std::chrono_literals;

class Benchmark {

private:
  std::vector<uint8_t> input_;
  size_t alphabet_size_;

  std::vector<size_t> access_queries_;
  std::vector<size_t> rank_characters_;

  pasta::MemoryMonitor& mem_monitor_ = pasta::MemoryMonitor::instance();

public:
  size_t prefix_size = {0};
  std::string input_path = "";
  size_t number_queries = 1'000'000;
  bool with_rank_prefetching = true;

  void run() {
    prepare();

    mem_monitor_.reset();
    mem_monitor_.reset();
    auto wm = pasta::make_wm<pasta::BitVector>(input_.begin(), input_.end(),
                                               alphabet_size_);

    auto const wm_mem = mem_monitor_.get_and_reset();
    
    std::this_thread::sleep_for(2000ms);

    for (size_t i = 0; i < access_queries_.size(); ++i) {
      auto q = access_queries_[i];
      PASTA_DO_NOT_OPTIMIZE(q);
    }

    std::this_thread::sleep_for(2000ms);
    size_t const query_time = run_queries(wm);
    
    std::cout << "RESULT "
              << "alphabet_size=" << alphabet_size_ << " "
              << "text_size=" << input_.size() << " "
              << "algo=wavelet_matrix_queries "
              << "time=" << query_time << " "
              << wm_mem
              << "\n";
  }

private:
  void prepare() {
    // Read prefix of file
    std::ifstream stream(input_path.c_str(), std::ios::in | std::ios::binary);
    if (!stream) {
      std::cerr << "File " << input_path << " not found\n";
      exit(1);
    }
    stream.seekg(0, std::ios::end);
    uint64_t size = stream.tellg();
    if (prefix_size > 0) {
      size = std::min(prefix_size, size);
    }
    stream.seekg(0);
    input_.resize(size);
    stream.read(reinterpret_cast<char *>(input_.data()), size);
    stream.close();

    // Compute effective alphabet and effective alphabet size
    alphabet_size_ = pasta::reduce_alphabet(input_.begin(), input_.end());

    // Generate random access queries
    std::random_device rnd_device;
    std::mt19937 mersenne_engine(rnd_device());
    std::uniform_int_distribution<uint64_t> dist(0, input_.size());
    for (size_t i = 0; i < number_queries; ++i) {
      access_queries_.push_back(dist(mersenne_engine));
    }

    std::uniform_int_distribution<uint64_t> rank_char(0, alphabet_size_ - 1);
    for (size_t i = 0; i < number_queries; ++i) {
      rank_characters_.push_back(rank_char(mersenne_engine));
    }
  }

  template <typename WXSturcture>
  size_t run_queries(WXSturcture const &wx) {
    size_t result = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < access_queries_.size(); ++i) {
      result += wx.rank(access_queries_[i], rank_characters_[i]);//wx[query];
      //res_pt[i] = result;
      //PASTA_DO_NOT_OPTIMIZE(result);
    }
    auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::high_resolution_clock::now() - start)
                       .count();
    std::cout << "result " << result << '\n';
    return elapsed;
  }  
}; // class Benchmark

int32_t main(int32_t argc, char const *argv[]) {
  tlx::CmdlineParser cp;

  cp.set_description("Simple Wavelet Tree/Wavelet Matrix Benchmark");
  cp.set_author("Florian Kurpicz <florian@kurpicz.org>");

  Benchmark bench;

  cp.add_param_string("input", bench.input_path, "Path to input file.");
  cp.add_bytes('n', "size", bench.prefix_size,
               "Size (in bytes unless stated "
               "otherwise) of the string that use to test our suffix array "
               "construction algorithms.");
  cp.add_bytes('q', "queries", bench.number_queries,
               "Number of queries tested. "
               "Default is 1'000'000.");
  cp.add_flag('p', "prefetching", bench.with_rank_prefetching, "Use prefetching "
              "in rank implementation");

  if (!cp.process(argc, argv)) {
    return -1;
  }

  bench.run();
}

/******************************************************************************/
