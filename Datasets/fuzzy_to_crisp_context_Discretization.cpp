#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

/*
FUZZY CONTEXT → CRISP CONTEXT TRANSFORMATION

Method: Scaling
- For each fuzzy attribute with distinct values {v1, v2, ..., vk}
- We create k new binary attributes in the crisp context
- Object o has crisp attribute i,j if fuzzy_value(o,i) == vj

Example:
Attribute 0 with values {0.3, 0.4, 0.8, 0.9}
→ 4 crisp attributes: attr_0_0.3, attr_0_0.4, attr_0_0.8, attr_0_0.9
If object has value 0.8 for attribute 0:
→ has: attr_0_0.3 (0.8≥0.3), attr_0_0.4, attr_0_0.8
→ does not have: attr_0_0.9 (0.8<0.9)

Compilation:
g++ -std=c++17 -O3 -o fuzzy_to_crisp fuzzy_to_crisp.cpp

Usage:
./fuzzy_to_crisp input_fuzzy.txt output_crisp.txt
*/

using namespace std;

const double EPSILON = 1e-9;

struct FuzzyContext {
    int num_objects;
    int num_attributes;
    vector<vector<double>> matrix; // [object][attribute]
};

struct CrispContext {
    int num_objects;
    int num_attributes;
    vector<vector<int>> matrix; // [object][attribute]

    // Mapping: crisp_attr_index -> (original_fuzzy_attr, threshold_value)
    vector<pair<int, double>> attr_mapping;
};

// Read the fuzzy context
FuzzyContext read_fuzzy_context(const string& filename) {
    FuzzyContext ctx;

    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: cannot open " << filename << endl;
        exit(1);
    }

    file >> ctx.num_objects >> ctx.num_attributes;

    ctx.matrix.resize(ctx.num_objects);
    for (int i = 0; i < ctx.num_objects; ++i) {
        ctx.matrix[i].resize(ctx.num_attributes);
        for (int j = 0; j < ctx.num_attributes; ++j) {
            file >> ctx.matrix[i][j];
        }
    }

    file.close();

    return ctx;
}

// Transform fuzzy → crisp using scaling
CrispContext fuzzy_to_crisp_scaling(const FuzzyContext& fuzzy) {
    CrispContext crisp;
    crisp.num_objects = fuzzy.num_objects;
    crisp.matrix.resize(crisp.num_objects);

    // For each fuzzy attribute, find the distinct values
    vector<set<double>> distinct_values(fuzzy.num_attributes);

    for (int attr = 0; attr < fuzzy.num_attributes; ++attr) {
        for (int obj = 0; obj < fuzzy.num_objects; ++obj) {
            double val = fuzzy.matrix[obj][attr];
            if (val > EPSILON) { // Ignore ~0 values
                distinct_values[attr].insert(val);
            }
        }
    }

    // Create crisp attributes
    int crisp_attr_idx = 0;
    for (int attr = 0; attr < fuzzy.num_attributes; ++attr) {
        // Sort distinct values in ascending order
        vector<double> sorted_vals(distinct_values[attr].begin(),
            distinct_values[attr].end());
        sort(sorted_vals.begin(), sorted_vals.end());

        for (double threshold : sorted_vals) {
            crisp.attr_mapping.push_back({ attr, threshold });
            crisp_attr_idx++;
        }
    }

    crisp.num_attributes = crisp_attr_idx;

    // Fill the crisp matrix
    for (int obj = 0; obj < crisp.num_objects; ++obj) {
        crisp.matrix[obj].resize(crisp.num_attributes);

        for (int crisp_attr = 0; crisp_attr < crisp.num_attributes; ++crisp_attr) {
            int fuzzy_attr = crisp.attr_mapping[crisp_attr].first;
            double threshold = crisp.attr_mapping[crisp_attr].second;
            double fuzzy_val = fuzzy.matrix[obj][fuzzy_attr];

            // Object has the crisp attribute if its fuzzy value == threshold
            crisp.matrix[obj][crisp_attr] = (fuzzy_val == threshold) ? 1 : 0;
        }
    }

    return crisp;
}

// Write the crisp context
void write_crisp_context(const string& filename, const CrispContext& crisp) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: cannot write " << filename << endl;
        exit(1);
    }

    file << crisp.num_objects << " " << crisp.num_attributes << endl;

    for (int obj = 0; obj < crisp.num_objects; ++obj) {
        for (int attr = 0; attr < crisp.num_attributes; ++attr) {
            file << crisp.matrix[obj][attr];
            if (attr < crisp.num_attributes - 1) {
                file << " ";
            }
        }
        file << endl;
    }

    file.close();
}

// Print statistics
void print_statistics(const FuzzyContext& fuzzy, const CrispContext& crisp) {
    cout << "============================================================" << endl;
    cout << "FUZZY CONTEXT → CRISP CONTEXT TRANSFORMATION" << endl;
    cout << "============================================================" << endl;

    cout << "\nFUZZY CONTEXT (input):" << endl;
    cout << "  Objects: " << fuzzy.num_objects << endl;
    cout << "  Attributes: " << fuzzy.num_attributes << endl;

    // Compute fuzzy density
    double total_fuzzy = 0;
    int count_fuzzy = 0;
    for (int i = 0; i < fuzzy.num_objects; ++i) {
        for (int j = 0; j < fuzzy.num_attributes; ++j) {
            total_fuzzy += fuzzy.matrix[i][j];
            if (fuzzy.matrix[i][j] > EPSILON) count_fuzzy++;
        }
    }
    double avg_fuzzy = total_fuzzy / (fuzzy.num_objects * fuzzy.num_attributes);
    double density_fuzzy = (double)count_fuzzy / (fuzzy.num_objects * fuzzy.num_attributes);

    cout << "  Average value: " << fixed << setprecision(3) << avg_fuzzy << endl;
    cout << "  Density (values > 0): " << (density_fuzzy * 100) << "%" << endl;

    cout << "\nCRISP CONTEXT (output):" << endl;
    cout << "  Objects: " << crisp.num_objects << endl;
    cout << "  Attributes: " << crisp.num_attributes << endl;

    // Compute crisp density
    int count_ones = 0;
    for (int i = 0; i < crisp.num_objects; ++i) {
        for (int j = 0; j < crisp.num_attributes; ++j) {
            if (crisp.matrix[i][j] == 1) count_ones++;
        }
    }
    double density_crisp = (double)count_ones / (crisp.num_objects * crisp.num_attributes);

    cout << "  Entries set to 1: " << count_ones << endl;
    cout << "  Density: " << (density_crisp * 100) << "%" << endl;

    cout << "\nATTRIBUTE MAPPING:" << endl;
    cout << "  (format: crisp_attr → fuzzy_attr[threshold])" << endl;

    int prev_fuzzy_attr = -1;
    for (int i = 0; i < crisp.num_attributes; ++i) {
        int fuzzy_attr = crisp.attr_mapping[i].first;
        double threshold = crisp.attr_mapping[i].second;

        if (fuzzy_attr != prev_fuzzy_attr) {
            if (prev_fuzzy_attr != -1) cout << endl;
            cout << "  Fuzzy attribute " << fuzzy_attr << ": ";
            prev_fuzzy_attr = fuzzy_attr;
        }

        cout << "[" << threshold << "] ";
    }
    cout << endl;

    cout << "\nEXPANSION FACTOR:" << endl;
    cout << "  Fuzzy → crisp attributes: "
        << fuzzy.num_attributes << " → " << crisp.num_attributes
        << " (×" << (double)crisp.num_attributes / fuzzy.num_attributes << ")" << endl;

    cout << "============================================================" << endl;
}

// Print a transformation example
void print_example(const FuzzyContext& fuzzy, const CrispContext& crisp, int obj_idx = 0) {
    if (obj_idx >= fuzzy.num_objects) return;

    cout << "\nTRANSFORMATION EXAMPLE (object " << obj_idx << "):" << endl;
    cout << "------------------------------------------------------------" << endl;

    cout << "Fuzzy values: [";
    for (int j = 0; j < fuzzy.num_attributes; ++j) {
        cout << fixed << setprecision(1) << fuzzy.matrix[obj_idx][j];
        if (j < fuzzy.num_attributes - 1) cout << ", ";
    }
    cout << "]" << endl;

    cout << "\nCrisp values: [";
    for (int j = 0; j < crisp.num_attributes; ++j) {
        cout << crisp.matrix[obj_idx][j];
        if (j < crisp.num_attributes - 1) cout << " ";
    }
    cout << "]" << endl;

    cout << "\nDetail:" << endl;
    for (int fuzzy_attr = 0; fuzzy_attr < fuzzy.num_attributes; ++fuzzy_attr) {
        double val = fuzzy.matrix[obj_idx][fuzzy_attr];
        cout << "  Attr " << fuzzy_attr << " (value=" << val << "): ";

        for (int crisp_attr = 0; crisp_attr < crisp.num_attributes; ++crisp_attr) {
            if (crisp.attr_mapping[crisp_attr].first == fuzzy_attr) {
                double threshold = crisp.attr_mapping[crisp_attr].second;
                int has_attr = crisp.matrix[obj_idx][crisp_attr];
                cout << "[" << threshold << ":" << (has_attr ? "✓" : "✗") << "] ";
            }
        }
        cout << endl;
    }
    cout << "------------------------------------------------------------" << endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <input_fuzzy.txt> <output_crisp.txt>" << endl;
        cerr << "\nExample:" << endl;
        cerr << "  " << argv[0] << " 4X3.txt 4X3_crisp.txt" << endl;
        return 1;
    }

    string input_file = argv[1];
    string output_file = argv[2];

    cout << "Reading fuzzy context: " << input_file << endl;
    FuzzyContext fuzzy = read_fuzzy_context(input_file);

    cout << "Transforming to crisp context..." << endl;
    CrispContext crisp = fuzzy_to_crisp_scaling(fuzzy);

    cout << "Writing crisp context: " << output_file << endl;
    write_crisp_context(output_file, crisp);

    print_statistics(fuzzy, crisp);
    print_example(fuzzy, crisp, 0);

    cout << "\n✓ Transformation completed successfully!" << endl;

    return 0;
}
