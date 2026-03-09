// g++ -std=c++17 -O3 -march=native -pthread -o fuzzy_inclose_hasse_bitset_L1_closure fuzzy_inclose_hasse_bitset_L1_closure.cpp
//./fuzzy_inclose_hasse_bitset_L1_closure input.txt output_intents.txt 200000 --closure
//# args: input output quota [--closure] [--closure_ops 50000000]

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <atomic>
#include <mutex>
#include <cstring>

using namespace std;

// ============================================================
// Fuzzy InClose + Exact Hasse (bitset dominance index) + Optional closure
// L1 only: levels {0.0, 0.1, ..., 1.0}  <=> q in {0..10}
// ============================================================

enum class ImplicationType { Lukasiewicz = 2, Goguen = 3 };

struct Context {
    int nObjects = 0;
    int nAttributes = 0;
    vector<vector<float>> I;
};

struct FuzzySet {
    vector<float> grades;
    FuzzySet() {}
    explicit FuzzySet(int size) : grades(size, 0.0f) {}
};

static atomic<long long> canonicityTests(0);
static atomic<long long> intentsComputed(0);
static atomic<long long> extentsComputed(0);

static inline float fuzzyImplication(float a, float b, ImplicationType t) {
    switch (t) {
        case ImplicationType::Lukasiewicz:
            return std::min(1.0f, 1.0f - a + b);
        case ImplicationType::Goguen:
            if (a <= b + 1e-6f) return 1.0f;
            if (a < 1e-6f) return 1.0f;
            return b / a;
    }
    return 1.0f;
}

static inline bool fuzzySetEqual(const FuzzySet& A, const FuzzySet& B, float eps = 1e-6f) {
    if (A.grades.size() != B.grades.size()) return false;
    for (size_t i = 0; i < A.grades.size(); ++i) {
        if (fabs(A.grades[i] - B.grades[i]) > eps) return false;
    }
    return true;
}

static inline FuzzySet intersectionMin(const FuzzySet& A, const FuzzySet& B) {
    FuzzySet C((int)A.grades.size());
    for (size_t i = 0; i < A.grades.size(); ++i) C.grades[i] = min(A.grades[i], B.grades[i]);
    return C;
}

static inline FuzzySet unionMax(const FuzzySet& A, const FuzzySet& B) {
    FuzzySet C((int)A.grades.size());
    for (size_t i = 0; i < A.grades.size(); ++i) C.grades[i] = max(A.grades[i], B.grades[i]);
    return C;
}

// Galois operators
static FuzzySet derivationUp(const FuzzySet& X, const Context& ctx, ImplicationType imp) {
    intentsComputed++;
    FuzzySet Y(ctx.nAttributes);
    for (int m = 0; m < ctx.nAttributes; ++m) {
        float val = 1.0f;
        for (int g = 0; g < ctx.nObjects; ++g) {
            val = min(val, fuzzyImplication(X.grades[g], ctx.I[g][m], imp));
        }
        Y.grades[m] = val;
    }
    return Y;
}

static FuzzySet derivationDown(const FuzzySet& Y, const Context& ctx, ImplicationType imp) {
    extentsComputed++;
    FuzzySet X(ctx.nObjects);
    for (int g = 0; g < ctx.nObjects; ++g) {
        float val = 1.0f;
        for (int m = 0; m < ctx.nAttributes; ++m) {
            val = min(val, fuzzyImplication(Y.grades[m], ctx.I[g][m], imp));
        }
        X.grades[g] = val;
    }
    return X;
}

// closure on intents: cl(Y) = (Y↓)↑
static FuzzySet closureIntent(const FuzzySet& Y, const Context& ctx, ImplicationType imp) {
    FuzzySet X = derivationDown(Y, ctx, imp);
    return derivationUp(X, ctx, imp);
}

// IsCanonical
static bool IsCanonical(const FuzzySet& A, const FuzzySet& B, const FuzzySet& C,
                        int j, const Context& ctx, ImplicationType imp) {
    canonicityTests++;
    for (int m = 0; m < j; ++m) {
        if (B.grades[m] < 1.0f - 1e-6f) {
            float l_star = 1.0f;
            for (int g = 0; g < ctx.nObjects; ++g) {
                l_star = min(l_star, fuzzyImplication(C.grades[g], ctx.I[g][m], imp));
            }
            if (l_star > B.grades[m] + 1e-6f) return false;
        }
    }
    return true;
}

// ============================================================
// Packed Intent (L1) : q in {0..10}, stored as uint8_t
// ============================================================
static constexpr int MAX_ATTR = 64;

struct PackedIntent {
    uint8_t q[MAX_ATTR];
};

struct PackedIntentHash {
    int nAttr;
    explicit PackedIntentHash(int n) : nAttr(n) {}
    size_t operator()(PackedIntent const& k) const noexcept {
        uint64_t h = 1469598103934665603ULL;
        for (int i = 0; i < nAttr; ++i) {
            h ^= (uint64_t)k.q[i];
            h *= 1099511628211ULL;
        }
        // avalanche
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return (size_t)h;
    }
};

struct PackedIntentEq {
    int nAttr;
    explicit PackedIntentEq(int n) : nAttr(n) {}
    bool operator()(PackedIntent const& a, PackedIntent const& b) const noexcept {
        return memcmp(a.q, b.q, (size_t)nAttr) == 0;
    }
};

static inline bool strict_subset(const PackedIntent& a, const PackedIntent& b, int nAttr) {
    bool strict = false;
    for (int i = 0; i < nAttr; ++i) {
        if (a.q[i] > b.q[i]) return false;
        if (a.q[i] < b.q[i]) strict = true;
    }
    return strict;
}

static inline uint64_t edge_key(uint32_t child, uint32_t parent) {
    return (uint64_t(child) << 32) | uint64_t(parent);
}

// ============================================================
// Bitset dominance index (exact, scalable)
// GE[a][t] = intents with q[a] >= t
// LE[a][t] = intents with q[a] <= t
// ============================================================

struct BitsetDomIndex {
    int nAttr = 0;
    static constexpr int LEVELS = 11; // 0..10
    size_t Vcap = 0;
    size_t words = 0;
    size_t curV = 0;

    vector<vector<vector<uint64_t>>> GE; // [attr][t][word]
    vector<vector<vector<uint64_t>>> LE; // [attr][t][word]

    BitsetDomIndex(int nAttr_, size_t Vcap_) : nAttr(nAttr_), Vcap(Vcap_) {
        words = (Vcap + 63) / 64;
        GE.assign(nAttr, vector<vector<uint64_t>>(LEVELS, vector<uint64_t>(words, 0ULL)));
        LE.assign(nAttr, vector<vector<uint64_t>>(LEVELS, vector<uint64_t>(words, 0ULL)));
    }

    void add(const PackedIntent& x) {
        size_t idx = curV++;
        size_t w = idx / 64;
        uint64_t bit = 1ULL << (idx % 64);
        for (int a = 0; a < nAttr; ++a) {
            int v = (int)x.q[a];
            for (int t = 0; t <= v; ++t) GE[a][t][w] |= bit;      // >= t
            for (int t = v; t <= 10; ++t) LE[a][t][w] |= bit;     // <= t
        }
    }

    void mask_tail(vector<uint64_t>& bs) const {
        if (curV == 0) return;
        size_t full = curV / 64;
        int rem = (int)(curV % 64);

        // zero beyond
        for (size_t i = full + (rem ? 1 : 0); i < words; ++i) bs[i] = 0ULL;
        if (rem) {
            uint64_t m = (1ULL << rem) - 1ULL;
            bs[full] &= m;
        }
    }

    void candidates_parents(const PackedIntent& x, vector<uint64_t>& out) const {
        out.assign(words, ~0ULL);
        for (int a = 0; a < nAttr; ++a) {
            int t = (int)x.q[a];
            const auto& bs = GE[a][t];
            for (size_t w = 0; w < words; ++w) out[w] &= bs[w];
        }
        mask_tail(out);
    }

    void candidates_children(const PackedIntent& x, vector<uint64_t>& out) const {
        out.assign(words, ~0ULL);
        for (int a = 0; a < nAttr; ++a) {
            int t = (int)x.q[a];
            const auto& bs = LE[a][t];
            for (size_t w = 0; w < words; ++w) out[w] &= bs[w];
        }
        mask_tail(out);
    }

    static void bitset_to_indices(const vector<uint64_t>& bs, vector<uint32_t>& idxs) {
        idxs.clear();
        for (size_t w = 0; w < bs.size(); ++w) {
            uint64_t x = bs[w];
            while (x) {
                int b = __builtin_ctzll(x);
                idxs.push_back((uint32_t)(w * 64 + (size_t)b));
                x &= (x - 1);
            }
        }
    }
};

// ============================================================
// StreamHasse: insert intents; maintain exact cover graph for current set
// + optional closure mode
// ============================================================

struct StreamHasse {
    int nAttr = 0;
    size_t quota = 0;

    vector<PackedIntent> intents;
    unordered_map<PackedIntent, uint32_t, PackedIntentHash, PackedIntentEq> id_of;
    BitsetDomIndex index;
    unordered_set<uint64_t> edges;

    atomic<bool> stop{false};

    // progress
    chrono::high_resolution_clock::time_point t0;
    chrono::high_resolution_clock::time_point lastPrint;

    // closure
    bool closure_mode = false;
    long long closure_ops_budget = 50000000; // safety
    atomic<long long> closure_ops_used{0};

    StreamHasse(int nAttr_, size_t quota_, bool closure, long long closure_budget)
        : nAttr(nAttr_),
          quota(quota_),
          id_of(0, PackedIntentHash(nAttr_), PackedIntentEq(nAttr_)),
          index(nAttr_, quota_),  // IMPORTANT: we cap index to quota (Option 1)
          closure_mode(closure),
          closure_ops_budget(closure_budget)
    {
        intents.reserve(quota);
        id_of.reserve(quota * 2);
        edges.reserve(quota * 4);
        t0 = chrono::high_resolution_clock::now();
        lastPrint = t0;
    }

    PackedIntent pack_L1(const FuzzySet& B) const {
        PackedIntent pi{};
        for (int m = 0; m < nAttr; ++m) {
            float v = B.grades[m];
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            int q = (int)llround(v * 10.0);
            if (q < 0) q = 0;
            if (q > 10) q = 10;
            pi.q[m] = (uint8_t)q;
        }
        for (int m = nAttr; m < MAX_ATTR; ++m) pi.q[m] = 0;
        return pi;
    }

    // Helpers: minima/maxima extraction among candidates
    void insert_minimal_parent(uint32_t cand, vector<uint32_t>& minima) const {
        for (uint32_t m : minima) {
            if (strict_subset(intents[m], intents[cand], nAttr)) return;
        }
        size_t w = 0;
        for (uint32_t m : minima) {
            if (!strict_subset(intents[cand], intents[m], nAttr)) minima[w++] = m;
        }
        minima.resize(w);
        minima.push_back(cand);
    }

    void insert_maximal_child(uint32_t cand, vector<uint32_t>& maxima) const {
        for (uint32_t m : maxima) {
            if (strict_subset(intents[cand], intents[m], nAttr)) return;
        }
        size_t w = 0;
        for (uint32_t m : maxima) {
            if (!strict_subset(intents[m], intents[cand], nAttr)) maxima[w++] = m;
        }
        maxima.resize(w);
        maxima.push_back(cand);
    }

    void maybe_print() {
        auto now = chrono::high_resolution_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - lastPrint).count() >= 2) {
            double elapsed = chrono::duration<double>(now - t0).count();
            double rate = intents.size() / max(1e-9, elapsed);
            double Ev = intents.empty() ? 0.0 : (double)edges.size() / (double)intents.size();
            cout << "  [progress] V=" << intents.size() << "/" << quota
                 << "  E=" << edges.size()
                 << "  E/V=" << fixed << setprecision(4) << Ev
                 << "  rate=" << fixed << setprecision(1) << rate << " concepts/s"
                 << "  closureOps=" << closure_ops_used.load()
                 << "  canTests=" << canonicityTests.load()
                 << "  up=" << intentsComputed.load()
                 << "  down=" << extentsComputed.load()
                 << "\n";
            lastPrint = now;
        }
    }

    // Insert packed intent directly (internal)
    bool add_packed_intent(const PackedIntent& x) {
        if (stop.load()) return false;
        if (intents.size() >= quota) { stop.store(true); return false; }

        auto it = id_of.find(x);
        if (it != id_of.end()) {
            maybe_print();
            return false; // duplicate
        }

        uint32_t x_idx = (uint32_t)intents.size();
        intents.push_back(x);
        id_of.emplace(intents.back(), x_idx);

        index.add(intents[x_idx]);

        // candidates
        vector<uint64_t> bsP, bsC;
        index.candidates_parents(intents[x_idx], bsP);
        index.candidates_children(intents[x_idx], bsC);

        // remove self
        {
            size_t w = x_idx / 64;
            bsP[w] &= ~(1ULL << (x_idx % 64));
            bsC[w] &= ~(1ULL << (x_idx % 64));
        }

        vector<uint32_t> candP, candC;
        BitsetDomIndex::bitset_to_indices(bsP, candP);
        BitsetDomIndex::bitset_to_indices(bsC, candC);

        vector<uint32_t> Pmin, Cmax;
        Pmin.reserve(16);
        Cmax.reserve(16);

        for (uint32_t p : candP)
            if (strict_subset(intents[x_idx], intents[p], nAttr))
                insert_minimal_parent(p, Pmin);

        for (uint32_t c : candC)
            if (strict_subset(intents[c], intents[x_idx], nAttr))
                insert_maximal_child(c, Cmax);

        for (uint32_t c : Cmax) edges.insert(edge_key(c, x_idx));
        for (uint32_t p : Pmin) edges.insert(edge_key(x_idx, p));

        // remove bypass edges
        for (uint32_t c : Cmax) {
            for (uint32_t p : Pmin) {
                uint64_t ek = edge_key(c, p);
                auto eit = edges.find(ek);
                if (eit != edges.end()) edges.erase(eit);
            }
        }

        maybe_print();
        if (intents.size() >= quota) stop.store(true);
        return true;
    }

    // Public: add intent from FuzzySet (quantize L1)
    bool add_intent(const FuzzySet& B) {
        PackedIntent x = pack_L1(B);
        return add_packed_intent(x);
    }

    // Convert packed -> FuzzySet (for closure computations only)
    FuzzySet unpack_to_fuzzy(const PackedIntent& x) const {
        FuzzySet B(nAttr);
        for (int m = 0; m < nAttr; ++m) B.grades[m] = 0.1f * (float)x.q[m];
        return B;
    }

    // Closure mode: whenever a new intent is inserted, we attempt to close under join:
    // join(Bi,Bj) = cl(max(Bi,Bj))
    // We do it incrementally with a queue. Exact until budget is exhausted.
    void closure_after_insert(uint32_t new_idx, const Context& ctx, ImplicationType imp) {
        if (!closure_mode) return;
        if (stop.load()) return;

        // queue of indices whose joins with others may generate new elements
        deque<uint32_t> q;
        q.push_back(new_idx);

        // We will repeatedly take an element a, and join with all current intents b.
        // Any new intent produced is inserted and also queued.
        while (!q.empty() && !stop.load()) {
            if (closure_ops_used.load() >= closure_ops_budget) {
                cerr << "\n[WARNING] closure budget exhausted (" << closure_ops_budget
                     << "). The set may NOT be fully closed under joins.\n";
                return;
            }

            uint32_t a_idx = q.front();
            q.pop_front();

            // Snapshot current size (it can grow)
            size_t curN = intents.size();

            FuzzySet A = unpack_to_fuzzy(intents[a_idx]);

            for (uint32_t b_idx = 0; b_idx < (uint32_t)curN; ++b_idx) {
                if (stop.load()) return;
                long long used = closure_ops_used.fetch_add(1) + 1;
                if (used >= closure_ops_budget) {
                    cerr << "\n[WARNING] closure budget exhausted (" << closure_ops_budget
                         << "). The set may NOT be fully closed under joins.\n";
                    return;
                }

                // join candidate
                FuzzySet B = unpack_to_fuzzy(intents[b_idx]);
                FuzzySet U = unionMax(A, B);
                FuzzySet J = closureIntent(U, ctx, imp); // cl(max)

                // Insert; if new, queue it
                if (add_intent(J)) {
                    uint32_t j_idx = (uint32_t)(intents.size() - 1);
                    q.push_back(j_idx);
                    if (stop.load()) return;
                }
            }
        }
    }
};

// ============================================================
// InClose2_ChildConcepts (streaming)
// ============================================================

static vector<float> buildLevels_L1() {
    vector<float> L;
    for (int i = 0; i <= 10; ++i) L.push_back(0.1f * (float)i);
    return L;
}

static void InClose2_ChildConcepts(
    FuzzySet& A,
    FuzzySet& B,
    int y,
    const Context& ctx,
    ImplicationType imp,
    const vector<float>& L,
    StreamHasse& hasse
) {
    if (hasse.stop.load()) return;

    // Insert current intent
    bool inserted = hasse.add_intent(B);
    if (inserted) {
        uint32_t new_idx = (uint32_t)(hasse.intents.size() - 1);
        hasse.closure_after_insert(new_idx, ctx, imp);
    }
    if (hasse.stop.load()) return;

    vector<tuple<FuzzySet, FuzzySet, int>> candidates;
    if (y > ctx.nAttributes) return;

    for (int j = y; j <= ctx.nAttributes; ++j) {
        for (int k = 1; k < (int)L.size(); ++k) {
            if (hasse.stop.load()) return;

            float l = L[k];
            if (B.grades[j-1] < l - 1e-6f) {
                FuzzySet singleAttr(ctx.nAttributes);
                singleAttr.grades[j-1] = l;

                FuzzySet attrExtent = derivationDown(singleAttr, ctx, imp);
                FuzzySet C = intersectionMin(A, attrExtent);

                if (fuzzySetEqual(C, A)) {
                    B.grades[j-1] = l;
                } else {
                    if (IsCanonical(A, B, C, j-1, ctx, imp)) {
                        FuzzySet D = B;
                        D.grades[j-1] = l;
                        candidates.push_back(make_tuple(C, D, j));
                    }
                }
            }
        }
    }

    for (auto& t : candidates) {
        if (hasse.stop.load()) return;
        FuzzySet C = get<0>(t);
        FuzzySet D = get<1>(t);
        int j = get<2>(t);
        InClose2_ChildConcepts(C, D, j + 1, ctx, imp, L, hasse);
    }
}

// ============================================================
// Metrics
// ============================================================

static size_t compute_height(size_t V,
                             const vector<vector<uint32_t>>& parents,
                             const vector<vector<uint32_t>>& children) {
    vector<int> indeg(V, 0);
    vector<size_t> level(V, 0);
    for (size_t i = 0; i < V; ++i) indeg[i] = (int)children[i].size();

    queue<uint32_t> q;
    for (uint32_t i = 0; i < (uint32_t)V; ++i) {
        if (indeg[i] == 0) { q.push(i); level[i] = 0; }
    }

    size_t max_level = 0;
    while (!q.empty()) {
        uint32_t u = q.front(); q.pop();
        max_level = max(max_level, level[u]);
        for (uint32_t p : parents[u]) {
            level[p] = max(level[p], level[u] + 1);
            if (--indeg[p] == 0) q.push(p);
        }
    }
    return max_level + 1;
}

static size_t compute_width(size_t V,
                            const vector<vector<uint32_t>>& parents,
                            const vector<vector<uint32_t>>& children) {
    vector<int> indeg(V, 0);
    vector<size_t> level(V, 0);
    for (size_t i = 0; i < V; ++i) indeg[i] = (int)children[i].size();

    queue<uint32_t> q;
    for (uint32_t i = 0; i < (uint32_t)V; ++i) {
        if (indeg[i] == 0) { q.push(i); level[i] = 0; }
    }

    while (!q.empty()) {
        uint32_t u = q.front(); q.pop();
        for (uint32_t p : parents[u]) {
            level[p] = max(level[p], level[u] + 1);
            if (--indeg[p] == 0) q.push(p);
        }
    }

    unordered_map<size_t,size_t> cnt;
    cnt.reserve(V);
    for (size_t l : level) cnt[l]++;
    size_t w = 0;
    for (auto& kv : cnt) w = max(w, kv.second);
    return w;
}

// ============================================================
// Export
// ============================================================

static void export_intents_file(const string& outpath,
                                const Context& ctx,
                                const StreamHasse& hasse) {
    ofstream fout(outpath);
    if (!fout.is_open()) throw runtime_error("cannot open output file");

    fout << ctx.nObjects << " " << ctx.nAttributes << " " << hasse.intents.size() << "\n";
    for (size_t i = 0; i < hasse.intents.size(); ++i) {
        fout << "I";
        for (int m = 0; m < ctx.nAttributes; ++m) {
            uint8_t q = hasse.intents[i].q[m];
            if (q) {
                double v = 0.1 * (double)q;
                fout << " " << fixed << setprecision(3) << v << "/" << m;
            }
        }
        fout << "\n";
    }
}

static void export_dot(const string& base, const StreamHasse& hasse) {
    string dot_path = base + ".dot";
    ofstream out(dot_path);
    if (!out.is_open()) {
        cerr << "WARNING: cannot write dot file\n";
        return;
    }
    out << "digraph fuzzy_lattice {\n";
    out << "  rankdir=BT;\n";
    out << "  node [shape=circle];\n";
    for (auto ek : hasse.edges) {
        uint32_t c = (uint32_t)(ek >> 32);
        uint32_t p = (uint32_t)(ek & 0xFFFFFFFFu);
        out << "  " << c << " -> " << p << ";\n";
    }
    out << "}\n";
    cout << "DOT exported to: " << dot_path << "\n";
}

static void export_log(const string& base,
                       size_t V, size_t E, double Ev,
                       size_t height, size_t width,
                       double seconds,
                       bool closure_mode,
                       long long closure_ops_used,
                       long long closure_budget) {
    string log_path = base + ".log";
    ofstream log(log_path);
    if (!log.is_open()) {
        cerr << "WARNING: cannot write log file\n";
        return;
    }
    log << "============================================================\n";
    log << "FUZZY SUB-LATTICE (L1) - EXACT HASSE (BITSET INDEX)\n";
    log << "============================================================\n";
    log << "|V| (vertices): " << V << "\n";
    log << "|E| (edges)   : " << E << "\n";
    log << "E/V           : " << Ev << "\n";
    log << "Height        : " << height << "\n";
    log << "Width         : " << width << "\n";
    log << "Time (s)      : " << seconds << "\n";
    log << "Closure mode  : " << (closure_mode ? "ON" : "OFF") << "\n";
    log << "Closure ops   : " << closure_ops_used << " / " << closure_budget << "\n";
    log << "Canonicity tests: " << canonicityTests.load() << "\n";
    log << "Intents computed : " << intentsComputed.load() << "\n";
    log << "Extents computed : " << extentsComputed.load() << "\n";
    log << "============================================================\n";
    cout << "Log exported to: " << log_path << "\n";
}

// ============================================================
// main
// ============================================================

int main(int argc, char** argv) {
    if (argc < 4) {
        cerr << "Usage: " << argv[0] << " <input_file> <output_intents_file> <quota> [--closure] [--closure_ops N]\n";
        cerr << "Example: " << argv[0] << " data.txt out.txt 200000 --closure --closure_ops 50000000\n";
        return 1;
    }

    string input = argv[1];
    string output = argv[2];
    size_t quota = (size_t)atoll(argv[3]);

    bool closure_mode = false;
    long long closure_budget = 50000000;

    for (int i = 4; i < argc; ++i) {
        string a = argv[i];
        if (a == "--closure") closure_mode = true;
        else if (a == "--closure_ops" && i + 1 < argc) {
            closure_budget = atoll(argv[++i]);
            if (closure_budget < 0) closure_budget = 0;
        }
    }

    ifstream fin(input);
    if (!fin.is_open()) {
        cerr << "Error: cannot open file " << input << "\n";
        return 1;
    }

    Context ctx;
    fin >> ctx.nObjects >> ctx.nAttributes;
    if (!fin || ctx.nAttributes <= 0 || ctx.nAttributes > MAX_ATTR) {
        cerr << "Error: invalid file header (or too many attributes > " << MAX_ATTR << ")\n";
        return 1;
    }

    ctx.I.assign(ctx.nObjects, vector<float>(ctx.nAttributes));
    for (int g = 0; g < ctx.nObjects; ++g) {
        for (int m = 0; m < ctx.nAttributes; ++m) {
            fin >> ctx.I[g][m];
            if (!fin) {
                cerr << "Error: reading I[" << g << "][" << m << "]\n";
                return 1;
            }
        }
    }
    fin.close();

    cout << "Context loaded: " << ctx.nObjects << " objects, "
         << ctx.nAttributes << " attributes\n\n";

    int impChoice;
    cout << "Choose implication:\n";
    cout << "  1 = Lukasiewicz\n";
    cout << "  2 = Goguen\n";
    cout << "Your choice: ";
    cin >> impChoice;

    ImplicationType imp = (impChoice == 1 ? ImplicationType::Lukasiewicz : ImplicationType::Goguen);

    vector<float> L = buildLevels_L1();

    cout << "\n=== Starting FuzzyInClose2 (L1) + Exact Hasse (bitset index) ===\n";
    cout << "Quota (includes ⊥ and ⊤) = " << quota << "\n";
    cout << "Closure mode = " << (closure_mode ? "ON" : "OFF")
         << " (budget " << closure_budget << " ops)\n\n";

    canonicityTests = 0;
    intentsComputed = 0;
    extentsComputed = 0;

    auto start = chrono::high_resolution_clock::now();

    StreamHasse hasse(ctx.nAttributes, quota, closure_mode, closure_budget);

    // ---- Force TOP (⊤) : A_top=1, B_top=Up(A_top) ----
    FuzzySet A_top(ctx.nObjects);
    for (int i = 0; i < ctx.nObjects; ++i) A_top.grades[i] = 1.0f;
    FuzzySet B_top = derivationUp(A_top, ctx, imp);
    if (hasse.add_intent(B_top)) {
        uint32_t idx = (uint32_t)(hasse.intents.size() - 1);
        hasse.closure_after_insert(idx, ctx, imp);
    }

    // ---- Force BOTTOM (⊥) : B_all1=1, A_bot=Down(B_all1), B_bot=Up(A_bot) ----
    FuzzySet B_all1(ctx.nAttributes);
    for (int m = 0; m < ctx.nAttributes; ++m) B_all1.grades[m] = 1.0f;
    FuzzySet A_bot = derivationDown(B_all1, ctx, imp);
    FuzzySet B_bot = derivationUp(A_bot, ctx, imp);
    if (hasse.add_intent(B_bot)) {
        uint32_t idx = (uint32_t)(hasse.intents.size() - 1);
        hasse.closure_after_insert(idx, ctx, imp);
    }

    if (quota <= hasse.intents.size()) hasse.stop.store(true);

    // ---- Enumerate concepts (streaming) ----
    FuzzySet A0 = A_top;
    FuzzySet B0(ctx.nAttributes); // original start intent
    InClose2_ChildConcepts(A0, B0, 1, ctx, imp, L, hasse);

    auto stop = chrono::high_resolution_clock::now();
    double seconds = chrono::duration<double>(stop - start).count();

    size_t V = hasse.intents.size();
    size_t E = hasse.edges.size();
    double Ev = (V ? (double)E / (double)V : 0.0);

    vector<vector<uint32_t>> parents(V), children(V);
    for (auto ek : hasse.edges) {
        uint32_t c = (uint32_t)(ek >> 32);
        uint32_t p = (uint32_t)(ek & 0xFFFFFFFFu);
        if (c < V && p < V) {
            parents[c].push_back(p);
            children[p].push_back(c);
        }
    }

    size_t height = compute_height(V, parents, children);
    size_t width  = compute_width(V, parents, children);

    cout << "\n=== FINAL RESULTS (SUB-LATTICE) ===\n";
    cout << "|V| (vertices) = " << V << "\n";
    cout << "|E| (edges)    = " << E << "\n";
    cout << "E/V            = " << fixed << setprecision(6) << Ev << "\n";
    cout << "Height         = " << height << "\n";
    cout << "Width          = " << width << "\n";
    cout << "Time (s)       = " << fixed << setprecision(3) << seconds << "\n";
    cout << "Closure mode   = " << (closure_mode ? "ON" : "OFF") << "\n";
    cout << "Closure ops    = " << hasse.closure_ops_used.load() << " / " << closure_budget << "\n";
    cout << "Canonicity tests: " << canonicityTests.load() << "\n";
    cout << "Intents computed: " << intentsComputed.load() << "\n";
    cout << "Extents computed: " << extentsComputed.load() << "\n";

    try {
        export_intents_file(output, ctx, hasse);
        cout << "\nIntents saved to: " << output << "\n";
    } catch (...) {
        cerr << "Error: cannot write intents file\n";
    }

    export_dot(output, hasse);
    export_log(output, V, E, Ev, height, width, seconds,
               closure_mode, hasse.closure_ops_used.load(), closure_budget);

    return 0;
}