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
            if (grades[i] > 1e-9f) {
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

// Global variables for statistics
int canonicityTests = 0;
int intentsComputed = 0;
int extentsComputed = 0;

// ==============================
// L-level generation
// ==============================

std::vector<float> buildLevels(LType type) {
    std::vector<float> levels;
    float step = 0.1f;
    
    if (type == LType::L2) step = 0.01f;
    if (type == LType::L2) step = 0.15f;
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

// ==============================
// Galois operators
// ==============================

FuzzySet derivationUp(const FuzzySet& X, const Context& ctx, ImplicationType imp) {
    intentsComputed++;
    FuzzySet Y(ctx.nAttributes);
    
    for (int m = 0; m < ctx.nAttributes; ++m) {
        float val = 1.0f;
        for (int g = 0; g < ctx.nObjects; ++g) {
            float impVal = fuzzyImplication(X.grades[g], ctx.I[g][m], imp);
            val = std::min(val, impVal);
        }
        Y.grades[m] = val;
    }
    
    return Y;
}

FuzzySet derivationDown(const FuzzySet& Y, const Context& ctx, ImplicationType imp) {
    extentsComputed++;
    FuzzySet X(ctx.nObjects);
    
    for (int g = 0; g < ctx.nObjects; ++g) {
        float val = 1.0f;
        for (int m = 0; m < ctx.nAttributes; ++m) {
            float impVal = fuzzyImplication(Y.grades[m], ctx.I[g][m], imp);
            val = std::min(val, impVal);
        }
        X.grades[g] = val;
    }
    
    return X;
}

inline FuzzySet intersection(const FuzzySet& A, const FuzzySet& B) {
    FuzzySet C(A.grades.size());
    for (size_t i = 0; i < A.grades.size(); ++i) {
        C.grades[i] = std::min(A.grades[i], B.grades[i]);
    }
    return C;
}

// ==============================
// IsCanonical
// ==============================

bool IsCanonical(const FuzzySet& A, const FuzzySet& B, const FuzzySet& C, 
                 int j, const Context& ctx, ImplicationType imp) {
    canonicityTests++;
    
    for (int m = 0; m < j; ++m) {
        if (B.grades[m] < 1.0f - 1e-6f) {
            float l_star = 1.0f;
            for (int g = 0; g < ctx.nObjects; ++g) {
                float impVal = fuzzyImplication(C.grades[g], ctx.I[g][m], imp);
                l_star = std::min(l_star, impVal);
            }
            
            if (l_star > B.grades[m] + 1e-6f) {
                return false;
            }
        }
    }
    
    return true;
}

// ==============================
// InClose2_ChildConcepts - Memory optimized
// ==============================

void InClose2_ChildConcepts(
    FuzzySet& A,
    FuzzySet& B,
    int y,
    const Context& ctx,
    ImplicationType imp,
    const std::vector<float>& L,
    std::vector<ConceptIntent>& intents  // Store intents only
) {
    // Add only the intent of the current concept
    ConceptIntent conceptIntent;
    conceptIntent.intent = B;
    intents.push_back(conceptIntent);
    
    std::vector<std::tuple<FuzzySet, FuzzySet, int>> candidates;
    
    if (y > ctx.nAttributes) {
        return;
    }
    
    for (int j = y; j <= ctx.nAttributes; ++j) {
        int k = 1;
        
        while (k < static_cast<int>(L.size())) {
            float l = L[k];
            
            if (B.grades[j-1] < l - 1e-6f) {
                FuzzySet singleAttr(ctx.nAttributes);
                singleAttr.grades[j-1] = l;
                
                FuzzySet attrExtent = derivationDown(singleAttr, ctx, imp);
                FuzzySet C = intersection(A, attrExtent);
                
                if (fuzzySetEqual(C, A)) {
                    B.grades[j-1] = l;
                }
                else {
                    if (IsCanonical(A, B, C, j-1, ctx, imp)) {
                        FuzzySet D = B;
                        D.grades[j-1] = l;
                        
                        candidates.push_back(std::make_tuple(C, D, j));
                    }
                }
            }
            
            k++;
        }
    }
    
    for (auto& tuple : candidates) {
        FuzzySet C = std::get<0>(tuple);
        FuzzySet D = std::get<1>(tuple);
        int j = std::get<2>(tuple);
        
        InClose2_ChildConcepts(C, D, j + 1, ctx, imp, L, intents);
    }
}

// ==============================
// FuzzyInClose2 - Memory optimized
// ==============================

std::vector<ConceptIntent> FuzzyInClose2(
    const Context& ctx,
    ImplicationType imp,
    const std::vector<float>& L
) {
    std::vector<ConceptIntent> intents;
    
    FuzzySet A_top(ctx.nObjects);
    for (int i = 0; i < ctx.nObjects; ++i) {
        A_top.grades[i] = 1.0f;
    }
    
    FuzzySet B_top(ctx.nAttributes);
    
    InClose2_ChildConcepts(A_top, B_top, 1, ctx, imp, L, intents);
    
    return intents;
}

// ==============================
// main
// ==============================

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file>\n";
        return 1;
    }
    
    std::ifstream fin(argv[1]);
    if (!fin.is_open()) {
        std::cerr << "Error: cannot open file " << argv[1] << "\n";
        return 1;
    }
    
    Context ctx;
    fin >> ctx.nObjects >> ctx.nAttributes;
    
    if (!fin) {
        std::cerr << "Error: invalid file format.\n";
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
    
    int impChoice;
    std::cout << "Choose implication:\n";
    std::cout << "  1 = Lukasiewicz\n";
    std::cout << "  2 = Goguen\n";
    std::cout << "Your choice: ";
    std::cin >> impChoice;
    
    ImplicationType imp = (impChoice == 1 ? ImplicationType::Lukasiewicz :
                                            ImplicationType::Goguen);
    
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
    
    std::cout << "\n=== Starting FuzzyInClose2 (Memory optimized mode + float) ===\n\n";
    
    canonicityTests = 0;
    intentsComputed = 0;
    extentsComputed = 0;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<ConceptIntent> intents = FuzzyInClose2(ctx, imp, L);
    
    auto stop = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration_sec = stop - start;
    
    std::cout << "\n=== RESULTS ===\n";
    std::cout << "Number of concepts: " << intents.size() << "\n";
    std::cout << "Execution time: " << std::fixed << std::setprecision(3) << duration_sec.count() << " seconds\n";
    std::cout << "Canonicity tests: " << canonicityTests << "\n";
    std::cout << "Intents computed: " << intentsComputed << "\n";
    std::cout << "Extents computed: " << extentsComputed << "\n";
    
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
    
    std::cout << "\nResults saved to: " << argv[2] << "\n";
    std::cout << "Note: Only intents are saved to conserve memory.\n";
    
    return 0;
}
