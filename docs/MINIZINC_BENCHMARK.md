# MiniZinc Benchmark Suite Results
**Date:** 2026-08-18 02:28:35  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.62 ms | 71.15 ms | 43.8x |
| Global Constraints | 16 | 16 | 100.0% | 4.37 ms | 70.22 ms | 16.1x |
| Other | 4 | 4 | 100.0% | 1.33 ms | 1302.44 ms | 983.0x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.67 ms | 72.69 ms | 43.5x |
| Industrial Scheduling | 4 | 4 | 100.0% | 21.93 ms | 70.45 ms | 3.2x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.74 ms | 70.23 ms | 40.3x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.15 ms | 74.34 ms | 34.7x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.48 ms | 83.40 ms | 33.7x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.67 | 70.29 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.70 | 72.11 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.61 | 69.57 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.58 | 70.24 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 71.02 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.76 | 69.40 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 69.75 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 71.51 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.69 | 72.89 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.54 | 69.45 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.70 | 68.86 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 69.71 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 72.09 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.99 | 69.00 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 68.69 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 69.22 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 70.30 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.59 | 69.38 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.64 | 75.83 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.73 | 70.34 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 71.44 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.62 | 70.28 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.69 | 70.57 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.84 | 70.60 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.70 | 69.53 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.64 | 69.33 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.91 | 69.75 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.60 | 71.43 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 70.18 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 46.00 | 70.36 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 69.49 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 69.20 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.62 | 69.28 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.65 | 70.05 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.80 | 69.32 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 70.78 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.75 | 70.29 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.65 | 69.38 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.80 | 69.84 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 69.23 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 70.00 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.60 | 69.50 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.61 | 69.73 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.87 | 70.52 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 70.08 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.60 | 69.03 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.90 | 71.03 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.70 | 82.06 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.69 | 70.92 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.66 | 68.65 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.91 | 74.24 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 69.51 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 70.51 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 71.97 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.73 | 74.64 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.85 | 71.95 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.75 | 73.64 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.75 | 75.33 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.79 | 83.12 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.97 | 75.75 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 2.01 | 75.06 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.67 | 70.71 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 71.87 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.81 | 71.89 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 73.43 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.69 | 70.50 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.69 | 73.44 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.76 | 73.17 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 73.50 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.09 | 88.35 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.69 | 80.43 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.68 | 82.37 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.71 | 78.21 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.70 | 77.61 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 93.42 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.94 | 97.50 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
