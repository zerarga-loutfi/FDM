#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <queue>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstring>
#include <random>

/*
ADVANCED FUZZY LATTICE METRICS - SCALABLE TO MILLIONS
======================================================
Techniques:
1. Incremental edge computation with batching
2. Size-stratified processing (group by cardinality)
3. Sparse storage (only store edges, not full adjacency)
4. Streaming height/width computation
5. Parallel batch processing

For datasets with 500K - 5M unique intents

Compilation:
g++ -std=c++17 -O3 -march=native -pthread -o fuzzy_exact fuzzy_exact_large_scale.cpp

Usage:
./fuzzy_exact input.txt [num_threads] [max_intents]
*/

using namespace std;

const double EPSILON = 1e-9;
const int MAX_ATTRIBUTES = 50;

struct CompactFuzzyIntent {
    float values[MAX_ATTRIBUTES];
    
    CompactFuzzyIntent() {
        memset(values, 0, sizeof(values));
    }
    
    bool operator==(const CompactFuzzyIntent& other) const {
        for (int i = 0; i < MAX_ATTRIBUTES; ++i) {
            if (fabs(values[i] - other.values[i]) > EPSILON) return false;
        }
        return true;
    }
    
    size_t hash(int num_attr) const {
        size_t h = 0;
        std::hash<int> int_hasher;
        for (int i = 0; i < num_attr; ++i) {
            if (values[i] > EPSILON) {
                int val_int = static_cast<int>(values[i] * 10000);
                h ^= (int_hasher(i) << 1) ^ int_hasher(val_int);
            }
        }
        return h;
    }
    
    double get_size(int num_attr) const {
        double sum = 0;
        for (int i = 0; i < num_attr; ++i) {
            sum += values[i];
        }
        return sum;
    }
    
    int get_cardinality(int num_attr) const {
        int count = 0;
        for (int i = 0; i < num_attr; ++i) {
            if (values[i] > EPSILON) count++;
        }
        return count;
    }
};

struct CompactFuzzyIntentHash {
    int num_attr;
    CompactFuzzyIntentHash(int n) : num_attr(n) {}
    size_t operator()(const CompactFuzzyIntent& intent) const {
        return intent.hash(num_attr);
    }
};

struct CompactFuzzyIntentEqual {
    bool operator()(const CompactFuzzyIntent& a, const CompactFuzzyIntent& b) const {
        return a == b;
    }
};

// Global state
int num_attributes = 0;
mutex cout_mutex;
mutex stats_mutex;
atomic<size_t> total_edges_found(0);
atomic<size_t> total_comparisons(0);
atomic<size_t> batches_processed(0);

// Sparse edge storage
struct EdgeList {
    vector<size_t> parents;  // Direct parents of this intent
    vector<size_t> children; // Direct children of this intent
};

CompactFuzzyIntent parse_fuzzy_intent_line(const string& line, int num_attr) {
    CompactFuzzyIntent intent;
    
    if (line.empty() || line[0] != 'I') return intent;
    
    const char* ptr = line.c_str() + 1;
    while (*ptr) {
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (!*ptr) break;
        
        char* end;
        double val = strtod(ptr, &end);
        if (end == ptr) break;
        
        ptr = end;
        if (*ptr != '/') continue;
        ptr++;
        
        long idx = strtol(ptr, &end, 10);
        if (end == ptr) break;
        ptr = end;
        
        if (idx >= 0 && idx < num_attr) {
            intent.values[idx] = static_cast<float>(val);
        }
    }
    
    return intent;
}

inline bool is_strict_fuzzy_subset(const CompactFuzzyIntent& child, 
                                   const CompactFuzzyIntent& parent, 
                                   int num_attr) {
    bool strict = false;
    for (int i = 0; i < num_attr; ++i) {
        if (child.values[i] > parent.values[i] + EPSILON) return false;
        if (fabs(child.values[i] - parent.values[i]) > EPSILON) strict = true;
    }
    return strict;
}

// Load and deduplicate intents
pair<vector<CompactFuzzyIntent>, map<int, vector<size_t>>> 
load_intents_stratified(const string& filepath, size_t max_intents) {
    
    cout << "[1/5] Loading and deduplicating intents..." << endl;
    
    ifstream file(filepath);
    if (!file.is_open()) {
        cerr << "ERROR: Cannot open file" << endl;
        return {};
    }
    
    string line;
    getline(file, line);
    
    stringstream header(line);
    int num_objects, expected_count;
    header >> num_objects >> num_attributes >> expected_count;
    
    cout << "  Objects: " << num_objects 
         << ", Attributes: " << num_attributes
         << ", Expected: " << expected_count << endl;
    
    if (num_attributes > MAX_ATTRIBUTES) {
        cerr << "ERROR: num_attributes > MAX_ATTRIBUTES" << endl;
        return {};
    }
    
    CompactFuzzyIntentHash hasher(num_attributes);
    CompactFuzzyIntentEqual comparer;
    unordered_map<CompactFuzzyIntent, size_t, CompactFuzzyIntentHash, CompactFuzzyIntentEqual> 
        unique_map(16, hasher, comparer);
    
    vector<CompactFuzzyIntent> intents_list;
    intents_list.reserve(min((size_t)expected_count, max_intents));
    
    size_t line_num = 0;
    size_t duplicates = 0;
    auto start = chrono::high_resolution_clock::now();
    
    while (getline(file, line) && intents_list.size() < max_intents) {
        CompactFuzzyIntent intent = parse_fuzzy_intent_line(line, num_attributes);
        
        auto it = unique_map.find(intent);
        if (it == unique_map.end()) {
            size_t idx = intents_list.size();
            unique_map[intent] = idx;
            intents_list.push_back(intent);
        } else {
            duplicates++;
        }
        
        line_num++;
        if (line_num % 500000 == 0) {
            auto now = chrono::high_resolution_clock::now();
            auto elapsed = chrono::duration_cast<chrono::seconds>(now - start).count();
            double rate = line_num / (elapsed + 1.0);
            
            cout << "  Loaded: " << line_num << " lines, "
                 << intents_list.size() << " unique, "
                 << duplicates << " dupes "
                 << "(" << (int)rate << " lines/s)\r" << flush;
        }
    }
    
    cout << endl;
    file.close();
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::seconds>(end - start).count();
    
    cout << "  Total: " << intents_list.size() << " unique intents from "
         << line_num << " lines in " << duration << "s" << endl;
    cout << "  Duplicates: " << duplicates 
         << " (" << (100.0 * duplicates / line_num) << "%)" << endl;
    
    // Stratify by cardinality
    map<int, vector<size_t>> by_cardinality;
    for (size_t i = 0; i < intents_list.size(); ++i) {
        int card = intents_list[i].get_cardinality(num_attributes);
        by_cardinality[card].push_back(i);
    }
    
    cout << "  Cardinality distribution:" << endl;
    for (const auto& p : by_cardinality) {
        cout << "    Card " << p.first << ": " << p.second.size() << " intents" << endl;
    }
    
    return {intents_list, by_cardinality};
}

// Compute edges in batches
void compute_edges_batch(
    const vector<CompactFuzzyIntent>& intents,
    const vector<size_t>& child_indices,
    const vector<size_t>& parent_indices,
    vector<EdgeList>& edge_lists,
    int thread_id,
    size_t batch_id) {
    
    size_t local_edges = 0;
    size_t local_comps = 0;
    
    for (size_t child_idx : child_indices) {
        const auto& child = intents[child_idx];
        double child_size = child.get_size(num_attributes);
        
        vector<size_t> candidate_parents;
        
        // Find all parents
        for (size_t parent_idx : parent_indices) {
            if (child_idx == parent_idx) continue;
            
            const auto& parent = intents[parent_idx];
            double parent_size = parent.get_size(num_attributes);
            
            if (parent_size < child_size - EPSILON) continue;
            
            local_comps++;
            if (is_strict_fuzzy_subset(child, parent, num_attributes)) {
                candidate_parents.push_back(parent_idx);
            }
        }
        
        // Filter to direct parents
        for (size_t p_idx : candidate_parents) {
            bool is_direct = true;
            
            for (size_t m_idx : candidate_parents) {
                if (m_idx == p_idx) continue;
                if (is_strict_fuzzy_subset(intents[m_idx], intents[p_idx], num_attributes)) {
                    is_direct = false;
                    break;
                }
            }
            
            if (is_direct) {
                lock_guard<mutex> lock(stats_mutex);
                edge_lists[child_idx].parents.push_back(p_idx);
                edge_lists[p_idx].children.push_back(child_idx);
                local_edges++;
            }
        }
    }
    
    total_edges_found += local_edges;
    total_comparisons += local_comps;
    batches_processed++;
    
    lock_guard<mutex> lock(cout_mutex);
    cout << "  Batch " << batch_id << " (thread " << thread_id << "): "
         << local_edges << " edges, "
         << local_comps << " comparisons" << endl;
}

// Parallel batch processing
vector<EdgeList> compute_cover_relation_stratified(
    const vector<CompactFuzzyIntent>& intents,
    const map<int, vector<size_t>>& by_cardinality,
    int num_threads) {
    
    cout << "\n[2/5] Computing cover relation (stratified by cardinality)..." << endl;
    
    vector<EdgeList> edge_lists(intents.size());
    vector<int> cardinalities;
    for (const auto& p : by_cardinality) {
        cardinalities.push_back(p.first);
    }
    sort(cardinalities.begin(), cardinalities.end());
    
    size_t batch_id = 0;
    auto start = chrono::high_resolution_clock::now();
    
    // Process by cardinality levels
    for (size_t i = 0; i < cardinalities.size(); ++i) {
        int child_card = cardinalities[i];
        const auto& children = by_cardinality.at(child_card);
        
        cout << "  Processing cardinality " << child_card 
             << " (" << children.size() << " intents)..." << endl;
        
        // Gather potential parents (cardinality >= child_card)
        vector<size_t> parent_pool;
        for (size_t j = i; j < cardinalities.size(); ++j) {
            const auto& parents = by_cardinality.at(cardinalities[j]);
            parent_pool.insert(parent_pool.end(), parents.begin(), parents.end());
        }
        
        // Split children into batches for parallel processing
        size_t batch_size = max((size_t)1, children.size() / (num_threads * 4));
        vector<thread> threads;
        
        for (size_t start_idx = 0; start_idx < children.size(); start_idx += batch_size) {
            size_t end_idx = min(start_idx + batch_size, children.size());
            
            vector<size_t> child_batch(children.begin() + start_idx, 
                                      children.begin() + end_idx);
            
            threads.emplace_back(compute_edges_batch,
                               ref(intents),
                               child_batch,
                               ref(parent_pool),
                               ref(edge_lists),
                               threads.size(),
                               batch_id++);
            
            // Limit concurrent threads
            if (threads.size() >= (size_t)num_threads) {
                for (auto& t : threads) t.join();
                threads.clear();
            }
        }
        
        for (auto& t : threads) t.join();
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::seconds>(end - start).count();
    
    cout << "  Total edges found: " << total_edges_found.load() << endl;
    cout << "  Total comparisons: " << total_comparisons.load() << endl;
    cout << "  Time: " << duration << "s" << endl;
    
    return edge_lists;
}

// Compute height using BFS
size_t compute_height(const vector<EdgeList>& edge_lists) {
    cout << "\n[3/5] Computing height (longest chain)..." << endl;
    auto start = chrono::high_resolution_clock::now();
    
    size_t n = edge_lists.size();
    vector<int> in_degree(n, 0);
    vector<size_t> level(n, 0);
    
    // Compute in-degrees
    for (size_t i = 0; i < n; ++i) {
        in_degree[i] = edge_lists[i].children.size();
    }
    
    // Topological sort with level computation
    queue<size_t> q;
    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            q.push(i);
            level[i] = 0;
        }
    }
    
    size_t max_level = 0;
    while (!q.empty()) {
        size_t node = q.front();
        q.pop();
        
        max_level = max(max_level, level[node]);
        
        for (size_t parent : edge_lists[node].parents) {
            level[parent] = max(level[parent], level[node] + 1);
            in_degree[parent]--;
            if (in_degree[parent] == 0) {
                q.push(parent);
            }
        }
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration<double>(end - start).count();
    
    size_t height = max_level + 1;
    cout << "  Height = " << height << " (computed in " << duration << "s)" << endl;
    
    return height;
}

// Compute width using level-based antichain
size_t compute_width(const vector<EdgeList>& edge_lists) {
    cout << "\n[4/5] Computing width (maximum antichain)..." << endl;
    auto start = chrono::high_resolution_clock::now();
    
    size_t n = edge_lists.size();
    vector<size_t> level(n, 0);

    // in_degree_up[i] = nombre d enfants de i pas encore traites.
    // Un noeud n est pousse dans la queue que quand tous ses enfants sont traites :
    // chaque noeud est visite exactement une fois (meme pattern que compute_height).
    vector<int> in_degree_up(n, 0);
    for (size_t i = 0; i < n; ++i)
        in_degree_up[i] = (int)edge_lists[i].children.size();

    queue<size_t> q;
    for (size_t i = 0; i < n; ++i) {
        if (in_degree_up[i] == 0) {
            q.push(i);
            level[i] = 0;
        }
    }

    while (!q.empty()) {
        size_t node = q.front();
        q.pop();

        for (size_t parent : edge_lists[node].parents) {
            level[parent] = max(level[parent], level[node] + 1);
            in_degree_up[parent]--;
            if (in_degree_up[parent] == 0)
                q.push(parent);
        }
    }
    
    // Count intents per level
    map<size_t, size_t> level_counts;
    for (size_t l : level) {
        level_counts[l]++;
    }
    
    size_t max_width = 0;
    for (const auto& p : level_counts) {
        max_width = max(max_width, p.second);
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration<double>(end - start).count();
    
    cout << "  Width = " << max_width << " (computed in " << duration << "s)" << endl;
    
    return max_width;
}

// Compute degree distribution
void compute_degree_stats(const vector<EdgeList>& edge_lists) {
    cout << "\n[5/5] Computing degree statistics..." << endl;
    
    vector<size_t> out_degrees, in_degrees;
    for (const auto& el : edge_lists) {
        out_degrees.push_back(el.parents.size());
        in_degrees.push_back(el.children.size());
    }
    
    sort(out_degrees.begin(), out_degrees.end());
    sort(in_degrees.begin(), in_degrees.end());
    
    double avg_out = 0, avg_in = 0;
    for (size_t d : out_degrees) avg_out += d;
    for (size_t d : in_degrees) avg_in += d;
    
    size_t n = edge_lists.size();
    avg_out /= n;
    avg_in /= n;
    
    size_t median_out = out_degrees[n/2];
    size_t median_in = in_degrees[n/2];
    
    cout << "  Average out-degree: " << avg_out << endl;
    cout << "  Median out-degree: " << median_out << endl;
    cout << "  Max out-degree: " << out_degrees.back() << endl;
    cout << "  Average in-degree: " << avg_in << endl;
    cout << "  Median in-degree: " << median_in << endl;
    cout << "  Max in-degree: " << in_degrees.back() << endl;
}

// Export edges in GraphViz DOT format
void export_dot(
    const string& filepath,
    const vector<EdgeList>& edge_lists)
{
    string dot_path = filepath + ".dot";
    ofstream out(dot_path);
    if (!out.is_open()) {
        cerr << "WARNING: Cannot write dot file: " << dot_path << endl;
        return;
    }

    out << "digraph fuzzy_lattice {\n";
    out << "    rankdir=BT;\n";  // bottom-to-top: child -> parent
    out << "    node [shape=circle];\n";

    for (size_t child_idx = 0; child_idx < edge_lists.size(); ++child_idx) {
        for (size_t parent_idx : edge_lists[child_idx].parents) {
            out << "    " << child_idx << " -> " << parent_idx << ";\n";
        }
    }

    out << "}\n";
    out.close();
    cout << "DOT  exported to: " << dot_path << endl;
}

// Export index file: one line per intent, format "idx attr1/val1 attr2/val2 ..."
void export_index(
    const string& filepath,
    const vector<CompactFuzzyIntent>& intents)
{
    string idx_path = filepath + "_index.txt";
    ofstream out(idx_path);
    if (!out.is_open()) {
        cerr << "WARNING: Cannot write index file: " << idx_path << endl;
        return;
    }

    out << "# index -> fuzzy intent\n";
    out << "# format: idx  val/attr val/attr ...\n";

    for (size_t i = 0; i < intents.size(); ++i) {
        out << i;
        for (int a = 0; a < num_attributes; ++a) {
            if (intents[i].values[a] > EPSILON) {
                out << "  " << intents[i].values[a] << "/" << a;
            }
        }
        out << "\n";
    }

    out.close();
    cout << "Index exported to: " << idx_path << endl;
}

void export_metrics_to_log(
    const string& filepath,
    size_t num_intents,
    size_t edges,
    size_t height,
    size_t width,
    double avg_degree,
    long long total_duration_s,
    size_t comparisons)
{
    // Build log filename: same base name as input + ".log"
    string log_path = filepath + ".log";

    ofstream log(log_path);
    if (!log.is_open()) {
        cerr << "WARNING: Cannot write log file: " << log_path << endl;
        return;
    }

    // Timestamp
    time_t now = time(nullptr);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

    log << "============================================================\n";
    log << "FUZZY LATTICE METRICS - LOG\n";
    log << "============================================================\n";
    log << "Date/Time       : " << ts                            << "\n";
    log << "Input file      : " << filepath                      << "\n";
    log << "------------------------------------------------------------\n";
    log << "|V| (vertices)  : " << num_intents                   << "\n";
    log << "|E| (edges)     : " << edges                         << "\n";
    log << "Average degree  : " << avg_degree                    << "\n";
    log << "Height          : " << height                        << "\n";
    log << "Width           : " << width                         << "\n";
    log << "------------------------------------------------------------\n";
    log << "Total comparisons: " << comparisons                  << "\n";
    log << "Execution time  : " << total_duration_s << "s ("
        << (total_duration_s / 60.0) << " min)\n";
    log << "============================================================\n";

    log.close();
    cout << "\nLog exported to: " << log_path << endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file> [num_threads] [max_intents]" << endl;
        cerr << "Example: " << argv[0] << " data.txt 16 2000000" << endl;
        return 1;
    }
    
    string filepath = argv[1];
    int num_threads = (argc > 2) ? atoi(argv[2]) : thread::hardware_concurrency();
    size_t max_intents = (argc > 3) ? atoll(argv[3]) : 5000000;
    
    cout << "============================================================" << endl;
    cout << "EXACT FUZZY LATTICE METRICS - LARGE SCALE" << endl;
    cout << "============================================================" << endl;
    cout << "File: " << filepath << endl;
    cout << "Threads: " << num_threads << endl;
    cout << "Max intents: " << max_intents << endl << endl;
    
    auto total_start = chrono::high_resolution_clock::now();
    
    // Load and stratify
    auto [intents, by_cardinality] = load_intents_stratified(filepath, max_intents);
    
    if (intents.empty()) {
        cerr << "ERROR: No intents loaded" << endl;
        return 1;
    }
    
    cout << "\nLoaded " << intents.size() << " unique intents" << endl;
    
    if (intents.size() > 1000000) {
        cout << "\nWARNING: Large dataset (" << intents.size() << " intents)" << endl;
        cout << "This may take several hours. Consider reducing max_intents." << endl;
        cout << "Estimated time: " << (intents.size() * intents.size() / 100000000) << " hours" << endl;
        cout << "\nPress Ctrl+C to cancel, or wait 5 seconds to continue..." << endl;
        this_thread::sleep_for(chrono::seconds(5));
    }
    
    // Compute edges
    auto edge_lists = compute_cover_relation_stratified(intents, by_cardinality, num_threads);
    
    // Compute metrics
    size_t height = compute_height(edge_lists);
    size_t width = compute_width(edge_lists);
    compute_degree_stats(edge_lists);
    
    auto total_end = chrono::high_resolution_clock::now();
    auto total_duration = chrono::duration_cast<chrono::seconds>(total_end - total_start).count();
    
    // Final results
    size_t V = intents.size();
    size_t E = total_edges_found.load();
    double avg_degree = (V > 0) ? (double)E / V : 0.0;
    
    cout << "\n============================================================" << endl;
    cout << "FINAL RESULTS" << endl;
    cout << "============================================================" << endl;
    cout << "|V| (vertices) = " << V << " unique intents" << endl;
    cout << "|E| (edges) = " << E << endl;
    cout << "Average out-degree = " << avg_degree << endl;
    cout << "Height (longest chain) = " << height << endl;
    cout << "Width (maximum antichain) = " << width << endl;
    cout << "------------------------------------------------------------" << endl;
    cout << "Total comparisons: " << total_comparisons.load() << endl;
    cout << "Memory per intent: " << sizeof(CompactFuzzyIntent) << " bytes" << endl;
    cout << "Total memory: " << (V * sizeof(CompactFuzzyIntent) / (1024.0 * 1024.0)) << " MB" << endl;
    cout << "============================================================" << endl;
    cout << "Total execution time: " << total_duration << "s ("
         << (total_duration / 60.0) << " minutes)" << endl;
    
    export_dot(filepath, edge_lists);
    export_index(filepath, intents);
    export_metrics_to_log(filepath, V, E, height, width, avg_degree,
                          total_duration, total_comparisons.load());

    return 0;
}
