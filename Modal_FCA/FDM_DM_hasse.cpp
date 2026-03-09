#include <iostream>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <deque>
#include <pthread.h>
#include <cstring>
#include <chrono>
#include <mutex>
#include <atomic>

// g++ -std=c++17 -pthread -O3 -march=native -mtune=native -flto -o hasse_pthread_optimized hasse_pthread_optimized.cpp
// g++ -std=c++17 -pthread -O3 -march=native -mtune=native -flto -o hasse_pthread_optimized hasse_pthread_optimized.cpp

using namespace std;

const int NUM_THREADS = 8;
const int BUFFER_SIZE = 10000;

mutex edge_mutex;
atomic<int> next_concept{0};

struct ThreadData {
    const vector<vector<int>>* concepts;
    const vector<int>* sizes;
    const vector<vector<int>>* by_size;  // Index by size
    int max_size;
    vector<pair<int, int>>* shared_edges;
    int n;
    int thread_id;
    size_t local_count;
};

inline bool is_strict_subset_fast(const vector<int>& A, const vector<int>& B, int sizeA, int sizeB) {
    if (sizeA >= sizeB) return false;
    
    int i = 0, j = 0;
    while (i < sizeA && j < sizeB) {
        if (A[i] < B[j]) return false;
        if (A[i] == B[j]) i++;
        j++;
    }
    return i == sizeA;
}

bool is_cover_fast(int idxA, int idxB,
    const vector<vector<int>>& concepts,
    const vector<int>& sizes,
    const vector<vector<int>>& by_size) {
    
    int sizeA = sizes[idxA];
    int sizeB = sizes[idxB];
    
    // Critical optimization: if the size difference is 1, it is necessarily a cover
    if (sizeB - sizeA == 1) return true;
    
    const auto& A = concepts[idxA];
    const auto& B = concepts[idxB];
    
    // Only check intermediate sizes
    for (int sizeC = sizeA + 1; sizeC < sizeB; ++sizeC) {
        for (int k : by_size[sizeC]) {
            const auto& C = concepts[k];
            
            if (is_strict_subset_fast(A, C, sizeA, sizeC) && 
                is_strict_subset_fast(C, B, sizeC, sizeB)) {
                return false;
            }
        }
    }
    return true;
}

void* build_hasse_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    const auto& concepts = *(data->concepts);
    const auto& sizes = *(data->sizes);
    const auto& by_size = *(data->by_size);
    auto& shared_edges = *(data->shared_edges);
    
    int n = data->n;
    int max_size = data->max_size;
    
    vector<pair<int, int>> local_buffer;
    local_buffer.reserve(BUFFER_SIZE);
    
    data->local_count = 0;
    int processed = 0;
    
    // Dynamic load balancing
    while (true) {
        int i = next_concept.fetch_add(1);
        if (i >= n) break;
        
        int sizeI = sizes[i];
        
        // Only check concepts with strictly larger size
        for (int target_size = sizeI + 1; target_size <= max_size; ++target_size) {
            for (int j : by_size[target_size]) {
                if (is_strict_subset_fast(concepts[i], concepts[j], sizeI, target_size)) {
                    if (is_cover_fast(i, j, concepts, sizes, by_size)) {
                        local_buffer.emplace_back(i, j);
                        data->local_count++;
                        
                        if (local_buffer.size() >= BUFFER_SIZE) {
                            lock_guard<mutex> lock(edge_mutex);
                            shared_edges.insert(shared_edges.end(), 
                                              local_buffer.begin(), 
                                              local_buffer.end());
                            local_buffer.clear();
                        }
                    }
                }
            }
        }
        
        processed++;
        if (processed % 1000 == 0) {
            cout << "Thread " << data->thread_id << ": " 
                 << processed << " concepts processed, " 
                 << data->local_count << " edges found\n";
        }
    }
    
    // Flush the final buffer
    if (!local_buffer.empty()) {
        lock_guard<mutex> lock(edge_mutex);
        shared_edges.insert(shared_edges.end(), 
                          local_buffer.begin(), 
                          local_buffer.end());
    }
    
    return nullptr;
}

vector<pair<int, int>> build_hasse_diagram(const set<set<int>>& concept_set) {
    cout << "Converting concepts to vectors...\n";
    vector<vector<int>> concepts;
    vector<int> sizes;
    concepts.reserve(concept_set.size());
    sizes.reserve(concept_set.size());
    
    int max_size = 0;
    for (const auto& s : concept_set) {
        concepts.emplace_back(s.begin(), s.end());
        sizes.push_back(s.size());
        max_size = max(max_size, (int)s.size());
    }
    
    int n = concepts.size();
    
    // Build the index by size
    cout << "Building size index...\n";
    vector<vector<int>> by_size(max_size + 1);
    for (int i = 0; i < n; ++i) {
        by_size[sizes[i]].push_back(i);
    }
    
    cout << "Size distribution:\n";
    for (int s = 0; s <= max_size; ++s) {
        if (!by_size[s].empty()) {
            cout << "  Size " << s << ": " << by_size[s].size() << " concepts\n";
        }
    }
    
    vector<pair<int, int>> shared_edges;
    size_t est_edges = (size_t)n * 10;
    shared_edges.reserve(min(est_edges, (size_t)10000000));
    
    // Reset the atomic counter
    next_concept.store(0);
    
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    
    cout << "Starting " << NUM_THREADS << " threads with dynamic load balancing...\n";
    auto start_time = chrono::high_resolution_clock::now();
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        thread_data[t].concepts = &concepts;
        thread_data[t].sizes = &sizes;
        thread_data[t].by_size = &by_size;
        thread_data[t].max_size = max_size;
        thread_data[t].shared_edges = &shared_edges;
        thread_data[t].n = n;
        thread_data[t].thread_id = t;
        thread_data[t].local_count = 0;
        
        pthread_create(&threads[t], nullptr, build_hasse_thread, &thread_data[t]);
    }
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        pthread_join(threads[t], nullptr);
        cout << "Thread " << t << " finished with " << thread_data[t].local_count << " edges\n";
    }
    
    auto end_time = chrono::high_resolution_clock::now();
    double elapsed = chrono::duration<double>(end_time - start_time).count();
    
    cout << "\nTotal edges found: " << shared_edges.size() << "\n";
    cout << "Actual memory used: " << (shared_edges.size() * sizeof(pair<int,int>)) / (1024*1024) << " MB\n";
    cout << "Build time: " << elapsed << " seconds\n";
    
    return shared_edges;
}

void export_dot(const set<set<int>>& concept_set, const vector<pair<int, int>>& edges, const string& filename) {
    if (edges.size() > 100000) {
        cout << "WARNING: " << edges.size() << " edges. DOT file will be very large.\n";
    }
    
    ofstream fout(filename);
    fout << "graph Hasse {\n";
    fout << "rankdir=BT;\n";
    fout << "node [shape=box];\n";

    vector<set<int>> concepts(concept_set.begin(), concept_set.end());

    for (size_t i = 0; i < concepts.size(); ++i) {
        fout << "  " << i << " [label=\"{";
        for (auto it = concepts[i].begin(); it != concepts[i].end(); ++it) {
            fout << *it;
            if (next(it) != concepts[i].end()) fout << ",";
        }
        fout << "}\"];\n";
    }

    for (const auto& [from, to] : edges) {
        fout << "  " << from << " -- " << to << ";\n";
    }

    fout << "}\n";
    fout.close();
    cout << "Hasse diagram exported to '" << filename << "'\n";
}

// Export the edges list (optional output)
void export_edges_list(const vector<pair<int, int>>& edges, const string& filename) {
    ofstream fout(filename);
    if (!fout.is_open()) {
        cerr << "Error: Could not open " << filename << " for writing.\n";
        return;
    }

    for (const auto& [from, to] : edges) {
        fout << from << " " << to << "\n";
    }

    fout.close();
    cout << "Edges list exported to '" << filename << "'\n";
}

int compute_height(int n, const vector<pair<int,int>>& hasse_edges) {
    if(n == 0) return 0;
    vector<vector<int>> adj(n);
    vector<int> indeg(n,0);
    for (auto e : hasse_edges) {
        int u = e.first, v = e.second;
        adj[u].push_back(v);
        indeg[v]++;
    }
    
    vector<int> topo;
    topo.reserve(n);
    deque<int> dq;
    for(int i=0;i<n;i++) if(indeg[i]==0) dq.push_back(i);
    while(!dq.empty()){
        int u = dq.front(); dq.pop_front();
        topo.push_back(u);
        for(int v: adj[u]){
            if(--indeg[v]==0) dq.push_back(v);
        }
    }
    
    if((int)topo.size()!=n){
        topo.clear();
        for(int i=0;i<n;i++) topo.push_back(i);
    }
    vector<int> dp(n,0);
    for(int u: topo){
        for(int v: adj[u]){
            dp[v] = max(dp[v], dp[u] + 1);
        }
    }
    int longest_edges = 0;
    for(int x: dp) longest_edges = max(longest_edges, x);
    return longest_edges + 1;
}

struct HopcroftKarp {
    int nL, nR;
    vector<vector<int>> g;
    vector<int> dist, matchL, matchR;

    HopcroftKarp(int left, int right): nL(left), nR(right), g(left) {
        matchL.assign(nL, -1);
        matchR.assign(nR, -1);
        dist.resize(nL);
    }

    void addEdge(int uL, int vR){
        g[uL].push_back(vR);
    }

    bool bfs(){
        deque<int> dq;
        for(int u=0; u<nL; u++){
            if(matchL[u] == -1){
                dist[u] = 0;
                dq.push_back(u);
            } else {
                dist[u] = -1;
            }
        }
        bool foundFree = false;
        while(!dq.empty()){
            int u = dq.front(); dq.pop_front();
            for(int v: g[u]){
                int u2 = matchR[v];
                if(u2 == -1){
                    foundFree = true;
                } else if(dist[u2] == -1){
                    dist[u2] = dist[u] + 1;
                    dq.push_back(u2);
                }
            }
        }
        return foundFree;
    }

    bool dfs(int u){
        for(int v: g[u]){
            int u2 = matchR[v];
            if(u2 == -1 || (dist[u2] == dist[u] + 1 && dfs(u2))){
                matchL[u] = v;
                matchR[v] = u;
                return true;
            }
        }
        dist[u] = -1;
        return false;
    }

    int maxMatching(){
        int matching = 0;
        while(bfs()){
            for(int u=0; u<nL; u++){
                if(matchL[u] == -1 && dfs(u)){
                    matching++;
                }
            }
        }
        return matching;
    }
};

struct WidthThreadData {
    const vector<vector<int>>* concepts;
    const vector<int>* sizes;
    const vector<vector<int>>* by_size;
    int max_size;
    HopcroftKarp* hk;
    vector<pair<int,int>> local_edges;
};

atomic<int> next_width_concept{0};

void* compute_width_thread(void* arg) {
    WidthThreadData* data = (WidthThreadData*)arg;
    const auto& concepts = *(data->concepts);
    const auto& sizes = *(data->sizes);
    const auto& by_size = *(data->by_size);
    int n = concepts.size();
    int max_size = data->max_size;
    
    while (true) {
        int i = next_width_concept.fetch_add(1);
        if (i >= n) break;
        
        int sizeI = sizes[i];
        for (int target_size = sizeI + 1; target_size <= max_size; ++target_size) {
            for (int j : by_size[target_size]) {
                if (is_strict_subset_fast(concepts[i], concepts[j], sizeI, target_size)) {
                    data->local_edges.emplace_back(i, j);
                }
            }
        }
    }
    
    return nullptr;
}

int compute_width_exact(const vector<vector<int>>& concepts, 
                        const vector<int>& sizes,
                        const vector<vector<int>>& by_size,
                        int max_size) {
    int n = concepts.size();
    if(n == 0) return 0;

    cout << "Computing width...\n";
    
    HopcroftKarp hk(n, n);
    
    next_width_concept.store(0);
    pthread_t threads[NUM_THREADS];
    WidthThreadData thread_data[NUM_THREADS];
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        thread_data[t].concepts = &concepts;
        thread_data[t].sizes = &sizes;
        thread_data[t].by_size = &by_size;
        thread_data[t].max_size = max_size;
        thread_data[t].hk = &hk;
        
        pthread_create(&threads[t], nullptr, compute_width_thread, &thread_data[t]);
    }
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        pthread_join(threads[t], nullptr);
    }
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        for (const auto& [i, j] : thread_data[t].local_edges) {
            hk.addEdge(i, j);
        }
    }
    
    int mm = hk.maxMatching();
    return n - mm;
}

void print_metrics(const vector<vector<int>>& concepts, 
                   const vector<int>& sizes,
                   const vector<vector<int>>& by_size,
                   int max_size,
                   const vector<pair<int,int>>& hasse_edges) {
    long long V = concepts.size();
    long long E = hasse_edges.size();
    double avgOutDeg = (V == 0) ? 0.0 : (double)E / (double)V;
    
    auto start = chrono::high_resolution_clock::now();
    int height = compute_height((int)V, hasse_edges);
    auto end = chrono::high_resolution_clock::now();
    double height_time = chrono::duration<double>(end - start).count();
    
    start = chrono::high_resolution_clock::now();
    int width = compute_width_exact(concepts, sizes, by_size, max_size);
    end = chrono::high_resolution_clock::now();
    double width_time = chrono::duration<double>(end - start).count();

    cout << "\n---- Metrics ----\n";
    cout << "|V| = " << V << "\n";
    cout << "|E| = " << E << "\n";
    cout << "avg out-degree (|E|/|V|) = " << avgOutDeg << "\n";
    cout << "height (longest chain length) = " << height << " (computed in " << height_time << "s)\n";
    cout << "width (maximum antichain size) = " << width << " (computed in " << width_time << "s)\n";
    cout << "-----------------\n";
}

int main(int argc, char* argv[]) {
    cout << "Optimized version using " << NUM_THREADS << " threads\n\n";

    // Command-line arguments:
    //   argv[1] = input file (optional; defaults to "FDMConcepts_For_Hasse_Diagramme.txt")
    //   argv[2] = DOT output file (optional; defaults to "hasse.dot")
    //   argv[3] = edges list output file (optional; if omitted, it is not generated)
    string input_file = "FDMConcepts_For_Hasse_Diagramme.txt";
    string dot_output = "hasse.dot";
    string edges_output;
    bool write_edges_file = false;

    if (argc >= 2) input_file = argv[1];
    if (argc >= 3) dot_output = argv[2];
    if (argc >= 4) {
        edges_output = argv[3];
        write_edges_file = true;
    }
    if (argc > 4) {
        cerr << "Warning: Extra arguments were ignored.\n"
             << "Usage: " << argv[0]
             << " [input_file] [dot_output_file] [edges_output_file]\n";
    }
    
    set<set<int>> FDMConcept_set;
    ifstream file(input_file);

    if (!file.is_open()) {
        cerr << "Error: Could not open " << input_file << "\n";
        return 1;
    }

    cout << "Loading concepts from file...\n";
    string line;
    size_t line_count = 0;
    while (getline(file, line)) {
        for (char& c : line) {
            if (c == '{' || c == '}' || c == ',') c = ' ';
        }

        stringstream ss(line);
        string token;
        set<int> concept;

        while (ss >> token) {
            if (!token.empty() && token[0] == 'a') {
                int idx = stoi(token.substr(1));
                concept.insert(idx);
            }
        }

        FDMConcept_set.insert(concept);
        
        line_count++;
        if (line_count % 100000 == 0) {
            cout << "Loaded " << line_count << " lines, " << FDMConcept_set.size() << " unique concepts\n";
        }
    }

    file.close();
    
    cout << "\nTotal: " << FDMConcept_set.size() << " unique concepts loaded\n\n";

    auto start = chrono::high_resolution_clock::now();
    vector<pair<int, int>> hasse = build_hasse_diagram(FDMConcept_set);
    auto end = chrono::high_resolution_clock::now();
    double build_time = chrono::duration<double>(end - start).count();
    
    // Rebuild structures for metrics
    vector<vector<int>> concepts;
    vector<int> sizes;
    concepts.reserve(FDMConcept_set.size());
    sizes.reserve(FDMConcept_set.size());
    
    int max_size = 0;
    for (const auto& s : FDMConcept_set) {
        concepts.emplace_back(s.begin(), s.end());
        sizes.push_back(s.size());
        max_size = max(max_size, (int)s.size());
    }
    
    vector<vector<int>> by_size(max_size + 1);
    for (int i = 0; i < (int)concepts.size(); ++i) {
        by_size[sizes[i]].push_back(i);
    }
    
    cout << "\n=================================\n";
    cout << "Hasse diagram built in " << build_time << " seconds\n";
    cout << "=================================\n";
    
    print_metrics(concepts, sizes, by_size, max_size, hasse);
    export_dot(FDMConcept_set, hasse, dot_output);
    if (write_edges_file) {
        export_edges_list(hasse, edges_output);
    }

    return 0;
}
