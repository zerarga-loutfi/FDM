#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <chrono>

// ==============================
// Types and enumerations
// ==============================

enum class ImplicationType {
    Lukasiewicz = 2,
    Goguen = 3
};

enum class LType {
    L1 = 1,
    L2 = 2,
    L3 = 3
};

// ==============================
// Structures
// ==============================

struct Context {
    int nObjects;
    int nAttributes;
    std::vector<std::vector<float>> I;
};

struct FuzzySet {
    std::vector<float> grades;
    
    FuzzySet() {}
    FuzzySet(int size) : grades(size, 0.0f) {}
    
    std::string toString() const {
        std::ostringstream oss;
        bool first = true;
        for (size_t i = 0; i < grades.size(); ++i) {
            if (grades[i] > 1e-6f) {
                if (!first) oss << " ";
                oss << std::fixed << std::setprecision(3) << grades[i] << "/" << i;
                first = false;
            }
        }
        return oss.str();
    }
};

// Lightweight structure: intent only
struct ConceptIntent {
    FuzzySet intent;
};

// Global variable for statistics
int closureCount = 0;

// ==============================
// L-level generation
// ==============================

std::vector<float> buildLevels(LType type) {
    std::vector<float> levels;
    float step = 0.1f;
    
    if (type == LType::L2) step = 0.01f;
    if (type == LType::L3) step = 0.001f;

    for (float x = 0.0f; x <= 1.0f + 1e-6f; x += step) {
        float v = std::round(x / step) * step;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        
        if (levels.empty() || std::fabs(v - levels.back()) > 1e-6f) {
            levels.push_back(v);
        }
    }
    
    if (std::fabs(levels.back() - 1.0f) > 1e-6f) {
        levels.push_back(1.0f);
    }

    return levels;
}

float quantize(float x, const std::vector<float>& levels) {
    float best = levels[0], bestDist = std::fabs(x - best);
    for (float v : levels) {
        float d = std::fabs(x - v);
        if (d < bestDist) {
            bestDist = d;
            best = v;
        }
    }
    return best;
}

// ==============================
// Fuzzy implications
// ==============================

inline float fuzzyImplication(float a, float b, ImplicationType t) {
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

// ==============================
// FuzzySet operations
// ==============================

inline bool fuzzySetEqual(const FuzzySet& A, const FuzzySet& B, float eps = 1e-6f) {
    if (A.grades.size() != B.grades.size()) return false;
    for (size_t i = 0; i < A.grades.size(); ++i) {
        if (std::fabs(A.grades[i] - B.grades[i]) > eps) return false;
    }
    return true;
}

void printFuzzySet(const FuzzySet& fs) {
    std::cout << "{";
    bool first = true;
    for (size_t i = 0; i < fs.grades.size(); ++i) {
        if (fs.grades[i] > 1e-6f) {
            if (!first) std::cout << ", ";
            std::cout << i << "/" << std::fixed << std::setprecision(3) << fs.grades[i];
            first = false;
        }
    }
    std::cout << "}";
}

// ==============================
// Fuzzy derivations (Galois)
// ==============================

FuzzySet derivationUp(const FuzzySet& A, const Context& ctx, 
                      const std::vector<float>& L, ImplicationType imp) {
    FuzzySet B(ctx.nAttributes);
    
    for (int m = 0; m < ctx.nAttributes; ++m) {
        float val = 1.0f;
        for (int g = 0; g < ctx.nObjects; ++g) {
            float impVal = fuzzyImplication(A.grades[g], ctx.I[g][m], imp);
            val = std::min(val, impVal);
        }
        B.grades[m] = quantize(val, L);
    }
    
    return B;
}

FuzzySet derivationDown(const FuzzySet& B, const Context& ctx,
                        const std::vector<float>& L, ImplicationType imp) {
    FuzzySet A(ctx.nObjects);
    
    for (int g = 0; g < ctx.nObjects; ++g) {
        float val = 1.0f;
        for (int m = 0; m < ctx.nAttributes; ++m) {
            float impVal = fuzzyImplication(B.grades[m], ctx.I[g][m], imp);
            val = std::min(val, impVal);
        }
        A.grades[g] = quantize(val, L);
    }
    
    return A;
}

// ==============================
// Next Closure - Helper functions
// ==============================

bool isSetPreceding(const FuzzySet& B, const FuzzySet& C, 
                   int a_i, float grade_i, float eps = 1e-6f) {
    
    for (int i = 0; i < a_i; ++i) {
        if (std::fabs(B.grades[i] - C.grades[i]) > eps) {
            return false;
        }
    }
    
    if (std::fabs(C.grades[a_i] - grade_i) > eps) return false;
    if (B.grades[a_i] >= C.grades[a_i] - eps) return false;
    
    return true;
}

bool computeDirectSum(const FuzzySet& A, int a_i, float grade_i, 
                     FuzzySet& result) {
    result = FuzzySet(A.grades.size());
    
    for (int i = 0; i < a_i; ++i) {
        result.grades[i] = A.grades[i];
    }
    
    if (A.grades[a_i] >= grade_i - 1e-6f) {
        return false;
    }
    
    result.grades[a_i] = grade_i;
    
    return true;
}

// ==============================
// Next Closure algorithm
// ==============================

FuzzySet computeNextIntent(const FuzzySet& A, const Context& ctx,
                          const std::vector<float>& L,
                          ImplicationType imp) {
    
    int n = ctx.nAttributes;
    
    for (int a_i = n - 1; a_i >= 0; a_i--) {
        
        for (size_t g_idx = 1; g_idx < L.size(); g_idx++) {
            float grade_i = L[g_idx];
            
            FuzzySet candB;
            if (!computeDirectSum(A, a_i, grade_i, candB)) {
                continue;
            }
            
            FuzzySet B_down = derivationDown(candB, ctx, L, imp);
            FuzzySet candB_closed = derivationUp(B_down, ctx, L, imp);
            closureCount++;
            
            if (isSetPreceding(A, candB_closed, a_i, grade_i)) {
                return candB_closed;
            }
        }
    }
    
    FuzzySet empty(ctx.nAttributes);
    return empty;
}

// ==============================
// Main algorithm - Memory optimized
// ==============================

std::vector<ConceptIntent> nextClosureConcepts(const Context& ctx, 
                                               ImplicationType imp,
                                               const std::vector<float>& L,
                                               bool verbose = false) {
    
    std::vector<ConceptIntent> intents;
    
    closureCount = 0;
    
    // Start with the empty set
    FuzzySet A(ctx.nAttributes);
    
    // Compute its closure
    FuzzySet A_down = derivationDown(A, ctx, L, imp);
    A = derivationUp(A_down, ctx, L, imp);
    closureCount++;
    
    // Add the first concept (intent only)
    ConceptIntent c;
    c.intent = A;
    intents.push_back(c);
    
    if (verbose) {
        std::cout << "Concept 1: Intent = ";
        printFuzzySet(A);
        std::cout << "\n";
    }
    
    bool isMaximal = false;
    
    while (!isMaximal) {
        
        FuzzySet A_next = computeNextIntent(A, ctx, L, imp);
        
        bool foundSuccessor = false;
        for (size_t i = 0; i < A_next.grades.size(); ++i) {
            if (A_next.grades[i] > 1e-6f) {
                foundSuccessor = true;
                break;
            }
        }
        
        if (!foundSuccessor) {
            break;
        }
        
        A = A_next;
        
        // Add the concept (intent only)
        ConceptIntent c;
        c.intent = A;
        intents.push_back(c);
        
        if (verbose && intents.size() % 1000 == 0) {
            std::cout << "Concept " << intents.size() << " found...\n";
        }
        
        // Check if we reached the maximum
        isMaximal = true;
        for (int i = 0; i < ctx.nAttributes; ++i) {
            if (A.grades[i] < 1.0f - 1e-6f) {
                isMaximal = false;
                break;
            }
        }
    }
    
    return intents;
}

// ==============================
// main
// ==============================

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <context_file> <output_file>\n";
        return 1;
    }
    
    // Read the context
    std::ifstream fin(argv[1]);
    if (!fin.is_open()) {
        std::cerr << "Error: cannot open file " << argv[1] << "\n";
        return 1;
    }
    
    Context ctx;
    fin >> ctx.nObjects >> ctx.nAttributes;
    if (!fin) {
        std::cerr << "Error: invalid file (n m).\n";
        return 1;
    }
    
    ctx.I.assign(ctx.nObjects, std::vector<float>(ctx.nAttributes));
    for (int g = 0; g < ctx.nObjects; ++g) {
        for (int m = 0; m < ctx.nAttributes; ++m) {
            fin >> ctx.I[g][m];
            if (!fin) {
                std::cerr << "Error: reading I[" << g << "][" << m << "].\n";
                return 1;
            }
        }
    }
    fin.close();
    
    std::cout << "Context loaded: " << ctx.nObjects << " objects, " 
              << ctx.nAttributes << " attributes\n\n";
    
    // Choose implication
    int impChoice;
    std::cout << "Choose implication:\n";
    std::cout << "  1 = Lukasiewicz\n";
    std::cout << "  2 = Goguen\n";
    std::cout << "Your choice: ";
    std::cin >> impChoice;
    
    ImplicationType imp = (impChoice == 1 ? ImplicationType::Lukasiewicz :
                                            ImplicationType::Goguen);
    
    // Choose L levels
    int Lchoice;
    std::cout << "\nChoose L levels:\n";
    std::cout << "  1 = L1 {0.0, 0.1, 0.2, ..., 0.9, 1.0}\n";
    std::cout << "  2 = L2 {0.00, 0.01, 0.02, ..., 0.99, 1.00}\n";
    std::cout << "  3 = L3 {0.000, 0.001, 0.002, ..., 0.999, 1.000}\n";
    std::cout << "Your choice: ";
    std::cin >> Lchoice;
    
    LType ltype = (Lchoice == 1 ? LType::L1 :
                   Lchoice == 2 ? LType::L2 :
                                  LType::L3);
    
    std::vector<float> L = buildLevels(ltype);
    
    // Verbose mode
    char verboseChoice;
    std::cout << "\nVerbose mode (display concepts)? (y/n): ";
    std::cin >> verboseChoice;
    bool verbose = (verboseChoice == 'y' || verboseChoice == 'Y');
    
    std::cout << "\n=== Starting Next Closure algorithm (Memory optimized mode + float) ===\n\n";
    
    // Run the algorithm
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<ConceptIntent> intents = nextClosureConcepts(ctx, imp, L, verbose);
    auto stop = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double> duration_sec = stop - start;
    
    std::cout << "\n=== RESULTS ===\n";
    std::cout << "Total number of concepts: " << intents.size() << "\n";
    std::cout << "Number of closures computed: " << closureCount << "\n";
    std::cout << "Execution time: " << std::fixed << std::setprecision(3) << duration_sec.count() << " seconds\n";
    
    // Write results - intents only
    std::ofstream fout(argv[2]);
    if (!fout.is_open()) {
        std::cerr << "Error: cannot open output file " << argv[2] << "\n";
        return 1;
    }
    
    fout << ctx.nObjects << " " << ctx.nAttributes << " " << intents.size() << "\n";
    
    for (const auto& conceptIntent : intents) {
        // Intent only
        fout << "I";
        for (int m = 0; m < ctx.nAttributes; ++m) {
            if (conceptIntent.intent.grades[m] > 1e-6f) {
                fout << " " << std::fixed << std::setprecision(3) 
                     << conceptIntent.intent.grades[m] << "/" << m;
            }
        }
        fout << "\n";
    }
    
    fout.close();
    
    std::cout << "Results saved to: " << argv[2] << "\n";
    std::cout << "Note: Only intents are saved to conserve memory.\n";
    
    return 0;
}
