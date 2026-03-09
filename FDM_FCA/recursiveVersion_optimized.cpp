/*
 * recursiveVersion_optimized.cpp
 * Optimized version without changing the base algorithm
 */

#include <iostream>
#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <algorithm>

using namespace std;

typedef vector<vector<float>> FuzzyContext;
typedef vector<int> AttributesVec;  // Changed from set to vector for performance

struct FuzzyObjects {
    vector<float> values;
    
    FuzzyObjects() = default;
    FuzzyObjects(int n, float val = 0.0f) : values(n, val) {}
    
    float& operator[](int i) { return values[i]; }
    const float& operator[](int i) const { return values[i]; }
    size_t size() const { return values.size(); }
};

// Custom hash for vector<int>
struct VectorHash {
    size_t operator()(const vector<int>& v) const {
        size_t seed = v.size();
        for (auto x : v) {
            seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

// Use unordered_set instead of set for O(1) lookup
unordered_set<AttributesVec, VectorHash> conceptsSet;
FuzzyContext fContext;
int n, m;

// Cache to avoid recomputations
unordered_map<AttributesVec, FuzzyObjects, VectorHash> objectsCache;
unordered_map<AttributesVec, AttributesVec, VectorHash> attributesCache;

// Load fuzzy context from file
FuzzyContext read_fuzzy_context(const string& filename) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Cannot open file: " << filename << endl;
        exit(1);
    }

    infile >> n >> m;
    FuzzyContext lcontext(n, vector<float>(m));
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            infile >> lcontext[i][j];

    infile.close();
    return lcontext;
}

// Optimized version using vector instead of set
inline FuzzyObjects compute_fuzzy_objects(const AttributesVec& B) {
    // Check the cache first
    auto it = objectsCache.find(B);
    if (it != objectsCache.end()) {
        return it->second;
    }
    
    FuzzyObjects O(n, 0.0f);
    
    if (B.empty()) {
        objectsCache[B] = O;
        return O;
    }
    
    // Optimisation : parcourir une seule fois
    for (int o = 0; o < n; o++) {
        float maxVal = 0.0f;
        for (int a : B) {
            maxVal = max(maxVal, fContext[o][a]);
        }
        O[o] = maxVal;
    }
    
    objectsCache[B] = O;
    return O;
}

// Optimized version with early exit and caching
inline AttributesVec compute_attributes(const FuzzyObjects& O) {
    AttributesVec A;
    A.reserve(m);  // Pre-allocation
    
    for (int a = 0; a < m; a++) {
        bool valid = true;
        for (int o = 0; o < n; o++) {
            if (fContext[o][a] > O[o]) {
                valid = false;
                break;  // Early exit
            }
        }
        if (valid)
            A.push_back(a);
    }
    
    return A;
}

// Optimized version with vector
inline bool contains(const AttributesVec& A, int value) {
    return find(A.begin(), A.end(), value) != A.end();
}

// Optimized generate version
void generate(AttributesVec& A, AttributesVec& B, int attr_idx) {
    // Optimized early return
    for (int i : B) {
        if (!contains(A, i) && i < attr_idx) {
            return;
        }
    }

    // Optimized insertion with unordered_set
    conceptsSet.insert(B);

    // Optimized loop
    for (int j = attr_idx + 1; j < m; j++) {
        AttributesVec A_clone = A;
        A_clone.push_back(j);
        
        AttributesVec new_B = B;
        new_B.push_back(j);
        sort(new_B.begin(), new_B.end());  // Keep sorted for consistent hashing
        
        FuzzyObjects X = compute_fuzzy_objects(new_B);
        AttributesVec new_Bbis = compute_attributes(X);
        
        generate(A_clone, new_Bbis, j);
    }
}

// Affichage de progression
class ProgressDisplay {
private:
    chrono::steady_clock::time_point lastUpdate;
    size_t lastCount;
    
public:
    ProgressDisplay() : lastUpdate(chrono::steady_clock::now()), lastCount(0) {}
    
    void update() {
        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::seconds>(now - lastUpdate).count();
        
        if (elapsed >= 2) {  // Update every 2 seconds
            size_t current = conceptsSet.size();
            cout << "\r[Progression] Concepts trouvés: " << current 
                 << " | Cache objets: " << objectsCache.size()
                 << " | Vitesse: " << (current - lastCount) / elapsed << " concepts/s"
                 << flush;
            
            lastUpdate = now;
            lastCount = current;
        }
    }
};

// Optimized printing with fewer allocations
void print_concepts_to_file(const unordered_set<AttributesVec, VectorHash>& concepts, 
                            const string& filenameWhole, 
                            const string& filenameToHassediag) {
    ofstream fileW(filenameWhole);
    if (!fileW.is_open()) {
        cerr << "Error: Could not open " << filenameWhole << " for writing.\n";
        return;
    }

    ofstream fileHD(filenameToHassediag);
    if (!fileHD.is_open()) {
        cerr << "Error: Could not open " << filenameToHassediag << " for writing.\n";
        return;
    }
    
    // Convertir en vector pour tri
    vector<AttributesVec> sortedConcepts(concepts.begin(), concepts.end());
    sort(sortedConcepts.begin(), sortedConcepts.end());
    
    int idx = 1;
    for (const auto& c : sortedConcepts) {
        fileW << "FDMConcept " << idx++ << ":\n";

        // Print attributes
        fileW << "Attributes: {";
        bool first = true;
        for (int a : c) {
            if (!first) fileW << ", ";
            fileW << "a" << a + 1;
            first = false;
        }

        fileHD << "{";
        first = true;
        for (int a : c) {
            if (!first) fileHD << ", ";
            fileHD << "a" << a + 1;
            first = false;
        }
        fileHD << "},\n";

        fileW << "}\nObjects:\n";

        FuzzyObjects O = compute_fuzzy_objects(c);

        // Print fuzzy objects
        for (size_t i = 0; i < O.size(); i++) {
            fileW << "  o" << i + 1 << ": "
                  << fixed << setprecision(1)
                  << O[i] << "\n";
        }
        fileW << "\n";
    }

    fileW.close();
    fileHD.close();
    cout << "\n'" << filenameWhole << "' generated successfully with " << concepts.size() << " concepts.\n";
    cout << "'" << filenameToHassediag << "' also generated successfully.\n";
}

int main(int argc, char* argv[]) {
    using namespace std::chrono;
    
    string filename = "fuzzy_context.txt";

    
        // Command-line arguments:

    
        //   argv[1] = input file (optional; defaults to "fuzzy_context.txt")

    
        //   argv[2] = output file (optional; defaults to "FDMConcepts.txt")

    
        //   argv[3] = output file for Hasse diagram (optional; defaults to "FDMConcepts_For_Hasse_Diagramme.txt")

    
        string outWhole = "FDMConcepts.txt";

    
        string outHasse = "FDMConcepts_For_Hasse_Diagramme.txt";


    
        if (argc >= 2) filename = argv[1];

    
        if (argc >= 3) outWhole = argv[2];

    
        if (argc >= 4) outHasse = argv[3];

    
        if (argc > 4) {

    
            cerr << "Warning: Extra arguments were ignored.\n"

    
                 << "Usage: " << argv[0]

    
                 << " [input_file] [output_file] [hasse_output_file]\n";

    
        }


    cout << "=== Chargement du contexte ===\n";
    fContext = read_fuzzy_context(filename);
    n = fContext.size();
    m = fContext[0].size();

    cout << "n = " << n << "  m = " << m << "\n";
    
    // Cache pre-allocation
    objectsCache.reserve(1000);
    conceptsSet.reserve(1000);
    
    cout << "\n=== Génération des concepts ===\n";
    auto start = high_resolution_clock::now();

    // Bottom concept
    AttributesVec emptyB;
    conceptsSet.insert(emptyB);
    
    ProgressDisplay progress;
    
    for (int a = 0; a < m; a++) {
        AttributesVec A;
        A.push_back(a);
        
        FuzzyObjects X = compute_fuzzy_objects(A);
        AttributesVec B = compute_attributes(X);
        
        generate(A, B, a);
        progress.update();
    }

    auto stop = high_resolution_clock::now();
    duration<double> duration_sec = stop - start;
    
    cout << "\n\n=== Résultats ===\n";
    cout << "Execution Time: " << fixed << setprecision(6) 
         << duration_sec.count() << " seconds\n";
    cout << "FDM-concepts number: " << conceptsSet.size() << "\n";
    cout << "Cache objets utilisé: " << objectsCache.size() << " entrées\n";

    cout << "\n=== Écriture des fichiers ===\n";
    print_concepts_to_file(conceptsSet, outWhole, outHasse);
    
    return 0;
}