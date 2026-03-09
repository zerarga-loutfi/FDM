/*
 * recursiveVersion.cpp
 *
 * Created on: 11 août 2025
 * Author: Zerarga Loutfi
 */

#include <iostream>
#include <vector>
#include <set>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <chrono>

using namespace std;

typedef vector<vector<float>> FuzzyContext;
typedef set<short> Attributes;
typedef vector<float> FuzzyObjects;

struct FDMConcept {
	Attributes attributes;
	FuzzyObjects objects;
};

std::set<Attributes> conceptsSet;
FuzzyContext fContext;
int n,m;
// Load the fuzzy context from a file
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

// Compute the fuzzy set of objects: O(o) = max_{a in B} I(o,a)
FuzzyObjects compute_fuzzy_objects(const Attributes& B) {
    FuzzyObjects O(n, 0.0f);
    for (int o = 0; o < n; o++) {
        for (int a : B) {
            O[o] = max(O[o], fContext[o][a]);
        }
    }
    return O;
}

// Compute the set of attributes (Formula 11)
Attributes compute_attributes(const FuzzyObjects& O) {
    Attributes A;
    for (int a = 0; a < m; a++) {
        bool valid = true;
        for (int o = 0; o < n; o++) {
            if (fContext[o][a]> O[o]) { //fuzzyContext[i][j] > X[i]
                valid = false;
                break;
            }
        }
        if (valid)
            A.insert(a);
    }
    return A;
}


void generate(Attributes& A, Attributes& B, int attr_idx) {
    int rest=0;
    for (int i : B) {
        if (A.find(i)==A.end() && i < attr_idx) {
            rest=1;
            return; //early return if any such i exists
            }
    }

    if (rest==0) {
    	//if (conceptsSet.find(B)==conceptsSet.end())
    	conceptsSet.insert(B);

        for (int j = attr_idx + 1; j < m; j++) {
        	Attributes A_clone = A;
            A_clone.insert(j);
            Attributes new_B = B;
            new_B.insert(j);
            FuzzyObjects X = compute_fuzzy_objects(new_B);
            Attributes new_Bbis = compute_attributes(X);
            generate(A_clone, new_Bbis, j);
        }
    }
}

// Print the set of FDMConcepts to files
void print_concepts_to_file(const set<Attributes>& concepts, const string& filenameWhole, const string& filenameToHassediag) {
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
    int idx = 1;
    for (const auto& c : concepts) {
    	fileW << "FDMConcept " << idx++ << ":\n";

        // Print attributes
        fileW << "Attributes: {";
        bool first = true;
        for (int a : c) {
            if (!first) fileW << ", ";
            fileW << "a" << a+1;
            first = false;
        }

        fileHD << "{";
        first = true;
        for (int a : c) {
            if (!first) fileHD << ", ";
            fileHD << "a" << a+1;
            first = false;
        }
        fileHD << "},\n";

        fileW << "}\nObjects:\n";

        FuzzyObjects O= compute_fuzzy_objects(c);


        // Print fuzzy objects
        for (size_t i = 0; i < O.size(); i++) {
            fileW << "  o" << i+1 << ": "
                 << fixed << setprecision(1)
                 << O[i] << "\n";
        }
        fileW << "\n";
    }

    fileW.close();
    fileHD.close();
    cout << "'" << filenameWhole << "' generated successfully with " << concepts.size() << " concepts.\n";
    cout << "'" << filenameToHassediag << "' also generated successfully.\n";
}


int main(int argc, char* argv[]) {
    using namespace std::chrono;
    int n, m;

    // Command-line arguments:
    //   argv[1] = input file (optional; defaults to "fuzzy_context.txt")
    //   argv[2] = output file (optional; defaults to "FDMConcepts.txt")
    //   argv[3] = output file for Hasse diagram (optional; defaults to "FDMConcepts_For_Hasse_Diagramme.txt")
    string filename = "fuzzy_context.txt";
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

    fContext = read_fuzzy_context(filename);
    n=fContext.size();
    m=fContext[0].size();

    cout << "n = "<<n<<"  m = "<<m<<"\n";
    auto start = high_resolution_clock::now();  // Start timer

    Attributes B;
    conceptsSet.insert(B);// Add the bottom FDM-concept
    for (int a = 0; a < m; a++) {
    	Attributes A;
        A.insert(a);
        FuzzyObjects X = compute_fuzzy_objects(A);
        Attributes B = compute_attributes(X);
        generate(A, B, a);
    }



    auto stop = high_resolution_clock::now();  // Stop timer
    duration<double> duration_sec = stop - start;
    cout << "\n Execution Time of the Recursive Version: " << fixed << setprecision(6) << duration_sec.count() << " seconds\n";
    cout << " FDM-concepts numbre is : "<<conceptsSet.size()<<"\n";

    print_concepts_to_file(conceptsSet, outWhole, outHasse);
    return 0;
}


