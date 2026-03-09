// quota_stratified_linked_sample.cpp
// g++ -O2 -std=c++17 -pthread quota_stratified_linked_sample.cpp -o quota_stratified_linked_sample
//
/*
./quota_sample input.txt output.txt 200000 42 8 2 5000
Arguments (dans l’ordre) :

input.txt output.txt

maxn (total max)

seed (shuffle reproductible)

threads

parent_levels : 1 / 2 / 3 (pool de parents)

max_parents_to_check : limite parents testés par candidat (ex: 5000) ou -1 illimité

*/
//
//


#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

struct Concept {
  std::vector<uint64_t> bits; // bitmask attributes
  int card = 0;
  std::string raw_line;       // original "I ..." line with newline
  int original_order = 0;     // for deterministic tiebreak
};

static inline int popcount64(uint64_t x) {
#if defined(__GNUG__)
  return __builtin_popcountll(x);
#else
  int c = 0;
  while (x) { x &= (x - 1); ++c; }
  return c;
#endif
}

static inline bool is_subset_bits(const std::vector<uint64_t>& a,
                                  const std::vector<uint64_t>& b) {
  // a ⊆ b  <=> (a & ~b) == 0
  const size_t n = a.size();
  for (size_t i = 0; i < n; ++i) {
    if (a[i] & ~b[i]) return false;
  }
  return true;
}

static Concept parse_intent_line(const std::string& line, int num_attributes, int order_id) {
  Concept c;
  c.original_order = order_id;
  c.raw_line = line;
  const int words = (num_attributes + 63) / 64;
  c.bits.assign(words, 0);

  // Expect "I <a1> <a2> ..."
  // We ignore lines that don't start with 'I'
  if (line.empty() || line[0] != 'I') return c;

  std::istringstream iss(line);
  std::string tag;
  iss >> tag; // "I"
  int idx;
  int card = 0;
  while (iss >> idx) {
    if (idx >= 0 && idx < num_attributes) {
      c.bits[idx / 64] |= (1ULL << (idx % 64));
      ++card;
    }
  }
  c.card = card;
  return c;
}

static std::vector<int> compute_sqrt_quotas(const std::vector<size_t>& counts, size_t maxn) {
  // counts[c] = number of candidates at cardinality c
  const int C = (int)counts.size() - 1;
  std::vector<int> quota(counts.size(), 0);

  if (maxn == 0) return quota;

  // Reserve card 0 up to 1 if exists
  size_t used0 = 0;
  if (counts[0] > 0) {
    quota[0] = 1;
    used0 = 1;
  }

  size_t budget = (maxn > used0) ? (maxn - used0) : 0;
  if (budget == 0) return quota;

  double sum_sqrt = 0.0;
  for (int c = 1; c <= C; ++c) sum_sqrt += std::sqrt((double)counts[c]);

  if (sum_sqrt <= 0.0) return quota;

  // Initial allocation
  size_t sumq = used0;
  for (int c = 1; c <= C; ++c) {
    if (counts[c] == 0) { quota[c] = 0; continue; }
    double share = std::sqrt((double)counts[c]) / sum_sqrt;
    int q = (int)std::floor(share * (double)budget);
    q = std::max(1, q);
    q = std::min<int>(q, (int)counts[c]);
    quota[c] = q;
    sumq += (size_t)q;
  }

  // If we exceeded maxn, reduce from the biggest quotas (but keep >=1 where counts>0)
  while (sumq > maxn) {
    int best = -1;
    int best_q = 0;
    for (int c = 1; c <= C; ++c) {
      if (quota[c] > best_q && quota[c] > 1) {
        best_q = quota[c];
        best = c;
      }
    }
    if (best == -1) break; // can't reduce further
    quota[best] -= 1;
    sumq -= 1;
  }

  // If we are under maxn, we *could* add 1 to some levels, but optional.
  // Keep it simple/deterministic: no refill here.

  return quota;
}

static void score_candidates_parallel(
    const std::vector<Concept>& candidates,
    const std::vector<const Concept*>& parent_pool,
    int threads,
    int max_parents_to_check, // to cap cost, -1 = no cap
    std::vector<int>& out_score) {

  const size_t n = candidates.size();
  out_score.assign(n, 0);
  if (n == 0) return;
  if (threads < 1) threads = 1;
  threads = std::min<int>(threads, (int)n);

  // Deterministic: we compute scores per index independently, then later select deterministically.
  auto worker = [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      int links = 0;
      int checked = 0;
      for (const Concept* p : parent_pool) {
        if (!p) continue;
        if (is_subset_bits(p->bits, candidates[i].bits)) {
          ++links;
        }
        if (max_parents_to_check > 0) {
          ++checked;
          if (checked >= max_parents_to_check) break;
        }
      }
      out_score[i] = links;
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(threads);
  size_t chunk = (n + (size_t)threads - 1) / (size_t)threads;
  for (int t = 0; t < threads; ++t) {
    size_t b = (size_t)t * chunk;
    size_t e = std::min(n, b + chunk);
    if (b >= e) break;
    pool.emplace_back(worker, b, e);
  }
  for (auto& th : pool) th.join();
}

static std::vector<int> pick_top_indices_deterministic(
    const std::vector<Concept>& candidates,
    const std::vector<int>& score,
    int need) {

  const int n = (int)candidates.size();
  std::vector<int> idx(n);
  std::iota(idx.begin(), idx.end(), 0);

  // Sort by score desc, then by candidates[i].original_order asc (reproducible after shuffle)
  std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
    if (score[a] != score[b]) return score[a] > score[b];
    return candidates[a].original_order < candidates[b].original_order;
  });

  if (need < 0) need = 0;
  if (need > n) need = n;
  idx.resize((size_t)need);
  return idx;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr
      << "Usage: " << argv[0] << " <input_file> <output_file> [maxn=200000] [seed=42] [threads=8]\n"
      << "Optional: [parent_levels=1|2|3 default=1] [max_parents_to_check=-1]\n"
      << "  parent_levels: pool parents from last_valid only (1), or last_valid + previous (2), or +2 previous (3)\n"
      << "  max_parents_to_check: cap how many parents are tested per candidate (speed tradeoff)\n";
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  size_t maxn = (argc >= 4) ? (size_t)std::stoull(argv[3]) : 200000;
  uint64_t seed = (argc >= 5) ? (uint64_t)std::stoull(argv[4]) : 42ULL;
  int threads = (argc >= 6) ? std::stoi(argv[5]) : 8;
  int parent_levels = (argc >= 7) ? std::stoi(argv[6]) : 1;            // 1..3
  int max_parents_to_check = (argc >= 8) ? std::stoi(argv[7]) : -1;    // -1 = no cap

  parent_levels = std::max(1, std::min(3, parent_levels));

  std::ifstream fin(input_file);
  if (!fin) {
    std::cerr << "Error: cannot open input " << input_file << "\n";
    return 1;
  }

  // Read header
  int num_objects = 0, num_attributes = 0;
  long long expected_count = 0;
  {
    std::string header;
    std::getline(fin, header);
    std::istringstream iss(header);
    if (!(iss >> num_objects >> num_attributes >> expected_count)) {
      std::cerr << "Error: bad header line\n";
      return 1;
    }
  }

  // Load all candidates grouped by cardinality
  const int max_card_possible = num_attributes; // worst case
  std::vector<std::vector<Concept>> by_card((size_t)max_card_possible + 1);
  std::vector<size_t> counts((size_t)max_card_possible + 1, 0);

  std::string line;
  int order_id = 0;
  while (std::getline(fin, line)) {
    if (line.empty() || line[0] != 'I') continue;
    line.push_back('\n'); // preserve newline in output
    Concept c = parse_intent_line(line, num_attributes, order_id++);
    if (c.card < 0 || c.card > max_card_possible) continue;
    by_card[(size_t)c.card].push_back(std::move(c));
    counts[(size_t)c.card] += 1;
  }
  fin.close();

  // Compute sqrt quotas
  std::vector<int> quota = compute_sqrt_quotas(counts, maxn);

  // Shuffle each cardinality list reproducibly (seed + card)
  for (size_t c = 0; c < by_card.size(); ++c) {
    auto& vec = by_card[c];
    if (vec.empty()) continue;
    std::mt19937_64 rng(seed + 1315423911ULL * (uint64_t)c);
    std::shuffle(vec.begin(), vec.end(), rng);
    // after shuffle, redefine original_order deterministically as position in shuffled array
    for (size_t i = 0; i < vec.size(); ++i) vec[i].original_order = (int)i;
  }

  // Selection result
  std::vector<std::vector<const Concept*>> selected_by_card(by_card.size());
  int last_valid = -1;

  auto build_parent_pool = [&](int last_valid_card) {
    std::vector<const Concept*> pool;
    if (last_valid_card < 0) return pool;
    // include up to parent_levels levels: last_valid, last_valid-1, last_valid-2 (if non-empty)
    int added = 0;
    for (int k = 0; k < parent_levels; ++k) {
      int c = last_valid_card - k;
      if (c < 0) break;
      if (!selected_by_card[(size_t)c].empty()) {
        pool.insert(pool.end(),
                    selected_by_card[(size_t)c].begin(),
                    selected_by_card[(size_t)c].end());
        ++added;
      }
    }
    (void)added;
    return pool;
  };

  auto select_level = [&](int cur_card, int need, int parent_levels_local, bool relax_if_needed) {
    // parent_levels_local: how many levels to pool for this attempt (1..3)
    int saved_parent_levels = parent_levels;
    parent_levels = parent_levels_local;
    std::vector<const Concept*> pool = build_parent_pool(last_valid);
    parent_levels = saved_parent_levels;

    const auto& candidates = by_card[(size_t)cur_card];
    std::vector<int> score;

    if (!pool.empty()) {
      score_candidates_parallel(candidates, pool, threads, max_parents_to_check, score);
      // Keep only those with score>0 (eligible)
      std::vector<int> eligible_idx;
      eligible_idx.reserve(candidates.size());
      for (int i = 0; i < (int)candidates.size(); ++i) {
        if (score[(size_t)i] > 0) eligible_idx.push_back(i);
      }

      // Sort eligible by score desc, then shuffled order asc
      std::stable_sort(eligible_idx.begin(), eligible_idx.end(), [&](int a, int b) {
        if (score[(size_t)a] != score[(size_t)b]) return score[(size_t)a] > score[(size_t)b];
        return candidates[(size_t)a].original_order < candidates[(size_t)b].original_order;
      });

      int take = std::min<int>(need, (int)eligible_idx.size());
      for (int k = 0; k < take; ++k) {
        selected_by_card[(size_t)cur_card].push_back(&candidates[(size_t)eligible_idx[(size_t)k]]);
      }

      if ((int)selected_by_card[(size_t)cur_card].size() >= need) return;
    }

    // Fallback:
    // If not enough eligible, optionally relax constraints or fill without constraints.
    if (!relax_if_needed) return;

    // Fill remaining with highest score (even if 0) OR simply next in shuffled order:
    // Here: prefer those with higher score if pool existed, else just take first.
    int have = (int)selected_by_card[(size_t)cur_card].size();
    int remaining = need - have;
    if (remaining <= 0) return;

    // Avoid duplicates: track picked indices
    std::unordered_set<int> picked;
    picked.reserve((size_t)need * 2);
    for (const Concept* p : selected_by_card[(size_t)cur_card]) {
      // find index by pointer arithmetic
      ptrdiff_t idx = p - &candidates[0];
      if (idx >= 0 && idx < (ptrdiff_t)candidates.size()) picked.insert((int)idx);
    }

    std::vector<int> idx((int)candidates.size());
    std::iota(idx.begin(), idx.end(), 0);

    // If score exists, sort by score desc; else keep shuffled order.
    if (!score.empty()) {
      std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (score[(size_t)a] != score[(size_t)b]) return score[(size_t)a] > score[(size_t)b];
        return candidates[(size_t)a].original_order < candidates[(size_t)b].original_order;
      });
    } // else idx is already in shuffled order because candidates were shuffled and original_order=i

    for (int i : idx) {
      if (remaining == 0) break;
      if (picked.find(i) != picked.end()) continue;
      selected_by_card[(size_t)cur_card].push_back(&candidates[(size_t)i]);
      picked.insert(i);
      --remaining;
    }
  };

  // Perform selection by increasing cardinality
  size_t total_selected = 0;

  // Level 0
  if (quota[0] > 0 && !by_card[0].empty()) {
    selected_by_card[0].push_back(&by_card[0][0]); // after shuffle, first is reproducible
    last_valid = 0;
    total_selected += 1;
  }

  for (int c = 1; c <= max_card_possible; ++c) {
    int need = quota[(size_t)c];
    if (need <= 0) continue;
    if (by_card[(size_t)c].empty()) continue;

    // Attempt 1: strict with configured parent_levels, no relax inside
    select_level(c, need, parent_levels, false);

    // If not enough: widen pool progressively up to 3 levels
    while ((int)selected_by_card[(size_t)c].size() < need) {
      int have = (int)selected_by_card[(size_t)c].size();
      int rem = need - have;

      bool filled = false;
      for (int pl = parent_levels + 1; pl <= 3; ++pl) {
        if (have >= need) break;
        // try again with bigger pool, but without using "fill without constraints" yet
        size_t before = selected_by_card[(size_t)c].size();
        select_level(c, need, pl, false);
        if (selected_by_card[(size_t)c].size() > before) { filled = true; break; }
      }
      if (!filled) {
        // Final fallback: fill remaining without constraint, but still prefer higher link score if available
        select_level(c, need, 3, true);
        break;
      }
    }

    if (!selected_by_card[(size_t)c].empty()) last_valid = c;
    total_selected += selected_by_card[(size_t)c].size();
    if (total_selected >= maxn) break;
  }

  // Write output
  std::ofstream fout(output_file);
  if (!fout) {
    std::cerr << "Error: cannot open output " << output_file << "\n";
    return 1;
  }

  // Count final
  size_t final_count = 0;
  for (auto& v : selected_by_card) final_count += v.size();

  fout << num_objects << " " << num_attributes << " " << final_count << "\n";
  for (size_t c = 0; c < selected_by_card.size(); ++c) {
    for (const Concept* p : selected_by_card[c]) {
      fout << p->raw_line;
    }
  }
  fout.close();

  // Log summary
  std::cerr << "Done. Selected " << final_count << " intents into " << output_file << "\n";
  std::cerr << "Quota summary (nonzero):\n";
  for (size_t c = 0; c < quota.size(); ++c) {
    if (quota[c] > 0) {
      std::cerr << "  card " << c
                << " counts=" << counts[c]
                << " quota=" << quota[c]
                << " selected=" << selected_by_card[c].size()
                << "\n";
    }
  }
  return 0;
}