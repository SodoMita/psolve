# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 18:37:58  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.68 ms | 77.52 ms | 46.2x |
| Global Constraints | 16 | 16 | 100.0% | 4.42 ms | 78.51 ms | 17.8x |
| Other | 4 | 4 | 100.0% | 1.38 ms | 1310.79 ms | 948.1x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.73 ms | 80.41 ms | 46.4x |
| Industrial Scheduling | 4 | 4 | 100.0% | 22.03 ms | 94.72 ms | 4.3x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.76 ms | 76.38 ms | 43.3x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.15 ms | 77.44 ms | 36.1x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.34 ms | 90.43 ms | 38.6x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.70 | 85.20 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.79 | 73.55 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.72 | 74.82 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.70 | 81.46 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 74.98 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.75 | 75.31 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 83.12 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 74.77 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.78 | 87.72 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.70 | 77.67 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.12 | 95.34 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 76.54 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 80.18 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.01 | 82.52 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 76.80 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 86.64 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 76.78 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.68 | 74.25 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.77 | 93.59 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.17 | 95.60 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 76.05 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.64 | 79.98 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.72 | 76.93 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.92 | 74.98 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.65 | 77.24 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.69 | 76.00 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.92 | 78.58 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.72 | 77.42 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 97.33 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 45.00 | 82.48 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 74.00 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 75.87 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.63 | 79.00 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.66 | 75.02 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.83 | 76.44 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 109.07 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.76 | 78.16 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.68 | 77.40 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.76 | 78.14 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 75.61 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 76.04 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.67 | 73.04 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.61 | 78.22 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.99 | 74.32 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 75.56 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.69 | 74.15 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.02 | 75.95 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.61 | 75.34 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.33 | 86.97 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.62 | 74.95 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.80 | 74.39 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 78.20 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.91 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 77.14 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.67 | 78.24 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.72 | 74.70 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.72 | 77.19 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.65 | 76.18 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.85 | 76.87 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.70 | 74.60 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.88 | 80.49 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.74 | 75.87 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 80.42 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.83 | 75.32 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 79.26 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.81 | 91.78 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.74 | 78.28 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.79 | 79.68 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 75.71 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 88.69 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.69 | 101.39 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.76 | 88.79 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.89 | 83.38 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.70 | 98.63 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 81.71 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.91 | 86.64 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
