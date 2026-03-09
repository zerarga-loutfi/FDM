#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <random>

int main() {

    // Read n and m
	int n,m;
    std::cout << "Enter number of objects (n): ";
    std::cin >> n;
    std::cout << "Enter number of attributes (m): ";
    std::cin >> m;

    // Random number generator for floats in [0, 1]
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.1, 0.9);

    // Context matrix
    std::vector<std::vector<double>> context_matrix(n, std::vector<double>(m));

    // Fill matrix with random values rounded to 1 decimal place
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            double value = dis(gen);
            context_matrix[i][j] = std::round(value * 10.0) / 10.0; // 1 decimal place
        }
    }

    // Write to context.txt
    std::ofstream file("fuzzy_context.txt");
    if (file.is_open()) {
        file << n << " " << m << "\n";
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < m; j++) {
                file << std::fixed << std::setprecision(1) << context_matrix[i][j];
                if (j < m - 1) file << " ";
            }
            file << "\n";
        }
        file.close();
        std::cout << "Fuzzy formal context generated with " << n << " objects and " << m << " attributes.\n";
        std::cout << "Output saved in 'fuzzy_context.txt' file.\n";
    } else {
        std::cerr << "Error: Could not open file for writing.\n";
    }

    return 0;
}
