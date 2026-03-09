/*
 * recursiveVersion_crisp.cpp
 * Version for crisp context (binary values {0,1})
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

typedef vector<vector<int>> CrispContext;  // Changed from float to int
typedef vector<int> AttributesVec;

struct CrispObjects {  // Changed from FuzzyObjects to CrispObjects
    vector<int> values;  // Changed from float to int
    
    CrispObjects() = default;
    CrispObjects(int n, int val = 0) : values(n, val) {}  // Changed float to int
    
    int& operator[](int i) { return values[i]; }
    const int& operator[](int i) const { return values[i]; }
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
CrispContext cContext;  // Changed from fContext to cContext
int n, m;

// Cache to avoid recomputations
unordered_map<AttributesVec, CrispObjects, VectorHash> objectsCache;
unordered_map<AttributesVec, AttributesVec, VectorHash> attributesCache;

// Load crisp context from file
CrispContext read_crisp_context(const string& filename) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Cannot open file: " << filename << endl;
        exit(1);
    }

    infile >> n >> m;
    CrispContext lcontext(n, vector<int>(m));  // Changed from float to int
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            infile >> lcontext[i][j];
            // Validation: ensure values are either 0 or 1
            if (lcontext[i][j] != 0 && lcontext[i][j] != 1) {
                cerr << "Error: Value at position (" << i << "," << j 
                     << ") is " << lcontext[i][j] << " but must be 0 or 1" << endl;
                exit(1);
            }
        }

    infile.close();
    return lcontext;
}

// Optimized version using vector instead of set
inline CrispObjects compute_crisp_objects(const AttributesVec& B) {
    // Check the cache first
    auto it = objectsCache.find(B);
    if (it != objectsCache.end()) {
        return it->second;
    }
    
    CrispObjects O(n, 0);  // Changed float to int
    
    if (B.empty()) {
        objectsCache[B] = O;
        return O;
    }
    
    // Optimisation : parcourir une seule fois
    for (int o = 0; o < n; o++) {
        int maxVal = 0;  // Changed from float to int
        for (int a : B) {
            maxVal = max(maxVal, cContext[o][a]);  // Changed fContext to cContext
        }
        O[o] = maxVal;
    }
    
    objectsCache[B] = O;
    return O;
}

// Optimized version with early exit and caching
inline AttributesVec compute_attributes(const CrispObjects& O) {
    AttributesVec A;
    A.reserve(m);  // Pre-allocation
    
    for (int a = 0; a < m; a++) {
        bool valid = true;
        for (int o = 0; o < n; o++) {
            if (cContext[o][a] > O[o]) {  // Changed fContext to cContext
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
        
        CrispObjects X = compute_crisp_objects(new_B);
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

        CrispObjects O = compute_crisp_objects(c);

        // Print crisp objects
        for (size_t i = 0; i < O.size(); i++) {
            fileW << "  o" << i + 1 << ": " << O[i] << "\n";  // Removed fixed and setprecision
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
    
    string filename = "crisp_context.txt";

    
        // Command-line arguments:

    
        //   argv[1] = input file (optional; defaults to "crisp_context.txt")

    
        //   argv[2] = output file (optional; defaults to "FDMConcepts.txt")

    
        //   argv[3] = output file for Hasse diagram (optional; defaults to "FDMConcepts_For_Hasse_Diagramme.txt")

    
        string outWhole = "DMConcepts.txt";

    
        string outHasse = "DMConcepts_For_Hasse_Diagramme.txt";


    
        if (argc >= 2) filename = argv[1];

    
        if (argc >= 3) outWhole = argv[2];

    
        if (argc >= 4) outHasse = argv[3];

    
        if (argc > 4) {

    
            cerr << "Warning: Extra arguments were ignored.\n"

    
                 << "Usage: " << argv[0]

    
                 << " [input_file] [output_file] [hasse_output_file]\n";

    
        }
        // Changed the file name

        cout << "=== Loading the Crisp Context ===\n";
        cContext = read_crisp_context(filename);  // Changed from fContext and read_fuzzy_context
        n = cContext.size();
        m = cContext[0].size();

        cout << "Number of objects (n) = " << n
            << "  Number of attributes (m) = " << m << "\n";

        // Cache pre-allocation
        objectsCache.reserve(1000);
        conceptsSet.reserve(1000);

        cout << "\n=== Computing DM-Concepts ===\n";
        auto start = high_resolution_clock::now();

        // Bottom concept
        AttributesVec emptyB;
        conceptsSet.insert(emptyB);

        ProgressDisplay progress;

        for (int a = 0; a < m; a++) {
            AttributesVec A;
            A.push_back(a);

            CrispObjects X = compute_crisp_objects(A);
            AttributesVec B = compute_attributes(X);

            generate(A, B, a);
            progress.update();
        }

        auto stop = high_resolution_clock::now();
        duration<double> duration_sec = stop - start;

        cout << "\n\n=== Results ===\n";
        cout << "Execution time: " << fixed << setprecision(6)
            << duration_sec.count() << " seconds\n";
        cout << "Number of DM concepts: " << conceptsSet.size() << "\n";

        cout << "\n=== Exporting DM-Concepts to Files ===\n";
        print_concepts_to_file(conceptsSet, outWhole, outHasse);

        cout << "\n=== Process Completed Successfully ===\n";
        return 0;
}
