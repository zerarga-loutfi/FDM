#include <iostream>
#include <fstream>
#include <vector>
#include <string>

/**
 * This program transforms a Fuzzy Formal Context into a Crisp Formal Context.
 * Logic: If fuzzy_value >= threshold, the result is 1, otherwise it is 0.
 * Usage: ./fuzzy_to_crisp <input_file> <alpha_threshold> <output_file>
 */

int main(int argc, char* argv[]) {
    // 1. Check command line arguments
    // Expecting: program name, input file, threshold, and output file
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <threshold> <output_file>" << std::endl;
        return 1;
    }

    std::string inputName = argv[1];
    double threshold = std::stod(argv[2]);
    std::string outputName = argv[3];

    // Open input file
    std::ifstream inputFile(inputName);
    if (!inputFile.is_open()) {
        std::cerr << "Error: Could not open input file " << inputName << std::endl;
        return 1;
    }

    // Open output file for writing
    std::ofstream outputFile(outputName);
    if (!outputFile.is_open()) {
        std::cerr << "Error: Could not create output file " << outputName << std::endl;
        return 1;
    }

    int n_objects, n_attributes;

    // 2. Read dimensions from the input file header 
    if (!(inputFile >> n_objects >> n_attributes)) {
        std::cerr << "Error: Invalid file format (header missing)." << std::endl;
        return 1;
    }

    // Write header to the output file
    outputFile << n_objects << " " << n_attributes << std::endl;

    // 3. Process the matrix values sequentially
    double value;
    int count = 0;

    while (inputFile >> value) {
        // Apply the alpha-cut logic: 1 if >= threshold, else 0
        if (value >= threshold) {
            outputFile << "1 ";
        }
        else {
            outputFile << "0 ";
        }

        count++;
        // Maintain the matrix structure (rows and columns) [cite: 4, 5]
        if (count % n_attributes == 0) {
            outputFile << std::endl;
        }
    }

    // Inform the user of success
    std::cout << "Success: Context transformed using threshold " << threshold << std::endl;
    std::cout << "Result saved to: " << outputName << std::endl;

    inputFile.close();
    outputFile.close();

    return 0;
}