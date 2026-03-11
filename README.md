# Fuzzy Dual Modal (FDM) Concept Analysis
### Algorithms and Hasse Diagram Construction for Fuzzy and Crisp Contexts

## Overview
This repository gathers several C++ implementations for computing concepts from fuzzy and crisp contexts.

It also includes programs to generate the **Hasse diagram** of the resulting concept lattice.

The code base contains multiple algorithmic families:
- FDM-Concepts algorithms : Recursive, recursive-optimized, and power-set variant,
- Dual Modal algorithm,
- Fuzzy FCA algorithms : In-Close and NextClosure,
- Datasets,
- Hasse diagram construction.


## Repository structure
```text
.
├── README.md
├── Datasets/        # datasets for experiments
├── FDM_FCA/         # fuzzy dual modal concept generation
├── Fuzzy_FCA/       # fuzzy FCA algorithms
└── Modal_FCA/       # crisp / modal FCA algorithm
```

### Directory summary
- **Datasets/**: example fuzzy and crisp contexts used as inputs for the programs.
- **FDM_FCA/**: FDM-Concept generation  and Hasse-diagram construction programs.
- **Fuzzy_FCA/**: Fuzzy In-Close, Fuzzy NextClosure, scalable Hasse processing (streaming), and quota-concepts sampling.
- **Modal_FCA/**: programs for modal FCA, including recursive generation and Hasse-diagram construction.

## Input data format
Most datasets follow the same pattern:

1. **first line**: number of objects and number of attributes
2. **next lines**: one row per object

Examples:

- fuzzy context values in **[0, 1]**
- crisp context values in **{0, 1}**

Example header:
```text
25 10
0.8 0.2 0.9 ...
```

## Main directories

### 1. `FDM_FCA/`
Programs for generating FDM concepts from fuzzy contexts and exporting concept descriptions for Hasse diagram construction.

Included programs:
- `recursiveVersion.cpp`
- `recursiveVersion_optimized.cpp`
- `powerSetVersion.cpp`
- `FDM_DM_hasse.cpp`

### 2. `Fuzzy_FCA/`
Programs for fuzzy formal concept analysis using different enumeration or large-scale processing strategies.

Included programs:
- `fuzzy_Inclose.cpp`
- `fuzzy_Nextclosure.cpp`
- `hasse_fuzzy_inclose.cpp`
- `hasse_fuzzy_large_scale.cpp`
- `quota_stratified.cpp`
- `quota_stratified_Stream.cpp`

### 3. `Modal_FCA/`
Programs dedicated to crisp contexts.

Included programs:
- `Crisp_recursiveVersion_optimized.cpp`
- `FDM_DM_hasse.cpp`

### 4. `Datasets/`
Ready-to-use datasets for experiments and benchmarks.

Available groups:
- small synthetic fuzzy contexts (`fuzzy_context_25X10.txt`, ..., `fuzzy_context_25X25.txt`)
- small synthetic crisp contexts (`crisp_context_25X10.txt`, ..., `crisp_context_25X25.txt`)
- scaled crisp datasets (`crisp_context_Scaling_*`)
- real or larger benchmark-style datasets (`Diabete`, `mushroom`, `parkinsons`)

Dataset generators : The repository includes utilities for generating or transforming datasets:

- **Random_Fuzzy_Formal_Context.cpp**  
  Generates synthetic fuzzy formal contexts.

- **Fuzzy_to_Crisp_context_threshold.cpp**  
  Converts fuzzy contexts to crisp contexts using a threshold.

- **fuzzy_to_crisp_context_Scaling.cpp**  
  Converts fuzzy contexts to crisp contexts using scaling.

## Compilation
Use a C++ compiler supporting **C++11 or newer**. Some programs require **C++17** and **pthread**.

### Typical compilation commands

#### FDM / recursive versions
```bash
g++ -std=c++11 -O3 FDM_FCA/recursiveVersion.cpp -o recursive_version
g++ -std=c++11 -O3 FDM_FCA/powerSetVersion.cpp -o powerset_version
g++ -std=c++17 -O3 FDM_FCA/recursiveVersion_optimized.cpp -o recursive_version_optimized
```

#### Hasse diagram generation
```bash
g++ -std=c++17 -pthread -O3 FDM_FCA/FDM_DM_hasse.cpp -o hasse_fdm
```

#### Fuzzy FCA
```bash
g++ -std=c++17 -O3  Fuzzy_FCA/fuzzy_Inclose.cpp -o fuzzy_inclose
g++ -std=c++17 -O3  Fuzzy_FCA/fuzzy_Nextclosure.cpp -o fuzzy_nextclosure
g++ -std=c++17 -O3 -pthread Fuzzy_FCA/hasse_fuzzy_inclose.cpp -o hasse_fuzzy_inclose
g++ -std=c++17 -O3 -pthread Fuzzy_FCA/hasse_fuzzy_large_scale.cpp -o hasse_fuzzy_large_scale
```

#### Sampling / scalable processing
```bash
g++  -std=c++17 -O3 -pthread Fuzzy_FCA/quota_stratified.cpp -o quota_stratified
g++  -std=c++17 -O3 -pthread Fuzzy_FCA/quota_stratified_Stream.cpp -o quota_stratified_stream
```

#### Modal / crisp FCA
```bash
g++ -std=c++17 -O3 Modal_FCA/Crisp_recursiveVersion_optimized.cpp -o crisp_recursive_optimized
g++ -std=c++17 -pthread -O3 Modal_FCA/FDM_DM_hasse.cpp -o modal_hasse
```

## Example runs

### Generate FDM concepts from a fuzzy context
```bash
./recursive_version Datasets/fuzzy_context_25X10.txt FDMConcepts.txt FDMConcepts_For_Hasse_Diagramme.txt
```

### Generate concepts with the optimized recursive version
```bash
./recursive_version_optimized Datasets/fuzzy_context_mushroom.txt FDMConcepts.txt FDMConcepts_For_Hasse_Diagramme.txt
```

### Build a Hasse diagram from a concept file
```bash
./hasse_fdm FDMConcepts_For_Hasse_Diagramme.txt hasse.dot
```

### Run fuzzy In-Close
```bash
./fuzzy_inclose Datasets/fuzzy_context_25X10.txt fuzzy_inclose_output.txt
```

### Run fuzzy NextClosure
```bash
./fuzzy_nextclosure Datasets/fuzzy_context_25X10.txt fuzzy_nextclosure_output.txt
```

### Run crisp/modal optimized generation
```bash
./crisp_recursive_optimized Datasets/crisp_context_25X10.txt crisp_concepts.txt crisp_hasse_input.txt
```

## Notes
- Several programs provide **default filenames** when arguments are omitted.
- Some Hasse-related programs produce **DOT files** that can be rendered with Graphviz.
- Large datasets such as **Diabetes**, **Mushroom**, and **Parkinsons** may generate a very large number of fuzzy concepts. To handle this situation, it is recommended to use `quota_stratified` or `quota_stratified_Stream` to extract a reduced subset of concepts. Alternatively, the program `hasse_fuzzy_inclose` can be used to directly extract fuzzy formal concepts while constructing the **Hasse diagram simultaneously**.

## Visualization of the Hasse Diagram
1. Install Graphviz (https://graphviz.org/download/).
2. Generate a PNG image by running:
   dot -Tpng hasse.dot -o hasse.png

Using an online viewer

If Graphviz is not installed locally, you can visualize the diagram online.

Open the Graphviz online viewer:
https://dreampuf.github.io/GraphvizOnline/

Copy and paste the content of the `hasse.dot` file into the editor.


## Suggested workflow
1. Choose a dataset from `Datasets/`.
2. Run a concept-generation program from `FDM_FCA/`, `Fuzzy_FCA/`, or `Modal_FCA/`.
3. Save the concept output intended for Hasse processing.
4. Run the Hasse program to generate `.dot`.
5. Visualize the resulting lattice with Graphviz or another compatible tool.

## Author
**Loutfi Zerarga**

