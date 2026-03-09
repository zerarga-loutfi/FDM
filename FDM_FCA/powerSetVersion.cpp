/*
 * powerSetVersion.cpp
 *
 *  Created on: 1 août 2025
 *      Author: Zerarga Loutfi
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
typedef set<int> Attributes;
typedef vector<float> FuzzyObjects;

struct FDMConcept{
	Attributes attributes;
	FuzzyObjects objects;
};

std::set<Attributes> conceptsSet;
FuzzyContext fContext;
int n,m;

// Load fuzzy context from file
FuzzyContext read_fuzzy_context(const string& filename, int& n, int& m) {
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

// Compute fuzzy set of objects: O(o) = max_{a in A} I(o,a)
FuzzyObjects compute_fuzzy_objects(const Attributes& A) {
    int n = fContext.size();
    FuzzyObjects O(n, 0.0f);
    for (int o = 0; o < n; o++) {
        for (int a : A) {
            O[o] = max(O[o], fContext[o][a]);
        }
    }
    return O;
}

// Compute attributes from fuzzy set of objects
Attributes compute_attributes(const FuzzyObjects& O) {
    int n = fContext.size();
    int m = fContext[0].size();
    Attributes A;
    for (int a = 0; a < m; a++) {
        bool valid = true;
        for (int o = 0; o < n; o++) {
            if (! (fContext[o][a]<= O[o])) {
                valid = false;
                break;
            }
        }
        if (valid)
            A.insert(a);
    }
    return A;
}

bool attributes_sets_equal(const Attributes& a, const Attributes& b) {
    return a == b;
}

//print the set of FDMConcepts in a file
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
    string filename = "Fuzzy_context.txt";

        // Command-line arguments:

        //   argv[1] = input file (optional; defaults to "Fuzzy_context.txt")

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


    fContext = read_fuzzy_context(filename, n, m);

    cout << "n = "<<n<<"  m = "<<m;

    auto start = high_resolution_clock::now();  // start timer

    int total = 1 << m;

    for (int i = 0; i < total; i++) {
    	Attributes A;
        for (int j = 0; j < m; j++)
            if (i & (1 << j))
                A.insert(j);

        FuzzyObjects O = compute_fuzzy_objects(A);
        Attributes Ap = compute_attributes(O);

        if (attributes_sets_equal(A, Ap)) {
        	conceptsSet.insert(A);
        }
    }
    auto stop = high_resolution_clock::now();  // stop timer
    duration<double> duration_sec = stop - start;
    cout << "\n Execution Time of the PowerSet Version: " << fixed << setprecision(6) << duration_sec.count() << " seconds\n";
    cout << " FDM-concepts numbre is : "<<conceptsSet.size()<<"\n";

    print_concepts_to_file(conceptsSet, outWhole, outHasse);
    return 0;
}



