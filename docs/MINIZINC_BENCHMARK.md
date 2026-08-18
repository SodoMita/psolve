# MiniZinc Benchmark Suite Results
**Date:** 2026-08-18 10:40:18  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.59 ms | 73.47 ms | 46.3x |
| Global Constraints | 16 | 16 | 100.0% | 4.43 ms | 69.55 ms | 15.7x |
| Other | 4 | 4 | 100.0% | 1.28 ms | 1302.09 ms | 1021.2x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.69 ms | 76.02 ms | 45.0x |
| Industrial Scheduling | 4 | 4 | 100.0% | 22.14 ms | 71.93 ms | 3.2x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.41 ms | 74.40 ms | 52.6x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.10 ms | 74.16 ms | 35.4x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.42 ms | 80.25 ms | 33.2x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.55 | 68.63 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.69 | 67.66 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.53 | 68.58 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.60 | 73.57 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 68.64 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.73 | 69.22 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 68.20 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 67.18 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.66 | 88.14 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.73 | 81.45 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.57 | 67.92 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 69.13 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 71.09 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.91 | 69.21 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 69.73 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 71.34 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 70.15 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.64 | 70.27 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.64 | 73.12 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.69 | 70.39 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 70.75 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.57 | 70.95 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.54 | 70.34 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.00 | 81.96 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.70 | 70.89 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.59 | 71.82 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.90 | 73.94 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.59 | 73.66 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 71.82 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 47.00 | 69.96 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 68.77 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 70.11 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.55 | 69.20 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.61 | 69.27 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.76 | 70.64 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 72.32 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.70 | 74.76 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.62 | 71.84 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.78 | 69.76 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 68.54 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 68.12 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.54 | 68.13 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.60 | 73.05 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.94 | 76.31 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 72.01 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.59 | 69.88 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.87 | 69.05 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.68 | 75.52 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.64 | 70.06 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.14 | 97.46 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.74 | 72.02 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 83.33 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 78.28 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 78.00 | 75.65 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.70 | 82.60 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.72 | 75.75 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.29 | 93.09 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.65 | 74.68 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.75 | 74.26 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.58 | 73.20 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.78 | 71.72 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.60 | 70.44 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 72.25 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.67 | 70.15 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 78.93 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.57 | 70.64 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.55 | 68.58 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.72 | 69.78 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 71.93 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.01 | 93.33 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.59 | 77.37 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.64 | 76.24 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.64 | 76.20 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.62 | 79.66 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 78.68 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.73 | 81.94 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
