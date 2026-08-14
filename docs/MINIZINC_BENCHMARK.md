# MiniZinc Benchmark Suite Results
**Date:** 2026-08-14 08:24:01  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.77 ms | 83.37 ms | 47.2x |
| Global Constraints | 16 | 16 | 100.0% | 5.38 ms | 82.17 ms | 15.3x |
| Other | 4 | 4 | 100.0% | 1.46 ms | 1311.13 ms | 898.0x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.81 ms | 90.25 ms | 49.9x |
| Industrial Scheduling | 4 | 4 | 100.0% | 21.98 ms | 84.64 ms | 3.9x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.07 ms | 81.18 ms | 39.2x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.55 ms | 86.67 ms | 34.0x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.78 ms | 87.29 ms | 31.4x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.82 | 83.96 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 2.01 | 89.07 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.08 | 79.49 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.73 | 83.90 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 88.21 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.84 | 83.46 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 77.99 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 76.52 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.03 | 91.25 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.85 | 108.68 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.91 | 80.08 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 87.27 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 82.68 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.23 | 80.10 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 84.56 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 5.00 | 83.76 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 81.84 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.85 | 80.86 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.92 | 89.17 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 3.05 | 109.74 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 76.35 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.75 | 83.58 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.22 | 77.86 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.33 | 80.16 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.66 | 85.52 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.74 | 75.88 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.00 | 81.60 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.63 | 85.23 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 98.11 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 56.00 | 77.29 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 4.00 | 76.16 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 73.21 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.61 | 77.26 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.76 | 79.71 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.83 | 80.87 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 76.66 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.78 | 76.48 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.93 | 118.51 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.89 | 82.10 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 74.37 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 80.51 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.06 | 76.25 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.62 | 74.60 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.71 | 79.97 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 6.00 | 76.59 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.09 | 77.59 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.87 | 85.97 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.76 | 76.57 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.44 | 87.59 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.24 | 87.14 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.88 | 77.74 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 124.10 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 86.71 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 83.71 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.89 | 90.85 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 2.00 | 87.76 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.11 | 93.65 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.74 | 81.27 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.79 | 81.14 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.86 | 88.77 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.96 | 84.47 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.71 | 75.73 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 78.18 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.97 | 82.60 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 76.67 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.76 | 77.77 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.06 | 77.75 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.00 | 112.06 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 111.17 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 88.80 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.65 | 86.11 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.66 | 88.46 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.70 | 84.35 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.67 | 81.91 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 94.11 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.78 | 97.47 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
