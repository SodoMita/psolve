# MiniZinc Benchmark Suite Results
**Date:** 2026-08-18 18:08:02  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.66 ms | 70.05 ms | 42.1x |
| Global Constraints | 16 | 16 | 100.0% | 4.44 ms | 70.98 ms | 16.0x |
| Other | 4 | 4 | 100.0% | 1.30 ms | 1302.03 ms | 1001.6x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.71 ms | 71.53 ms | 42.0x |
| Industrial Scheduling | 4 | 4 | 100.0% | 22.05 ms | 70.86 ms | 3.2x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.73 ms | 74.26 ms | 42.9x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.17 ms | 71.13 ms | 32.8x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.35 ms | 85.01 ms | 36.1x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.57 | 68.22 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 2.68 | 70.82 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.61 | 67.98 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.60 | 70.33 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 70.21 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.80 | 69.51 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 70.15 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 70.58 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.60 | 74.99 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.50 | 67.92 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.20 | 71.36 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 69.38 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 69.70 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.98 | 71.95 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 71.37 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 69.04 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 70.62 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.64 | 70.77 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.78 | 83.89 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.74 | 69.24 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 73.78 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.69 | 72.64 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.68 | 84.76 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.86 | 69.38 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.65 | 68.64 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.63 | 67.81 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.91 | 69.02 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.66 | 73.45 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 70.26 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 46.00 | 77.38 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 70.52 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 69.64 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.64 | 67.40 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.66 | 68.81 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.85 | 68.58 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 71.71 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.14 | 73.83 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.59 | 67.57 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.78 | 68.82 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 69.93 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 68.60 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.62 | 70.80 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.64 | 71.10 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.75 | 71.02 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 68.45 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.59 | 69.33 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.91 | 69.86 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.60 | 69.74 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.72 | 69.29 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.64 | 68.10 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.56 | 69.52 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 69.54 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 68.89 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 70.13 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.68 | 72.01 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.65 | 69.25 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.96 | 69.41 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 3.19 | 71.59 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.76 | 70.02 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.59 | 69.56 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.84 | 70.12 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.72 | 74.16 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 72.39 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.74 | 73.17 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 71.03 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.59 | 70.74 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.71 | 69.73 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.68 | 68.44 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 72.81 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 100.38 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.82 | 84.15 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.80 | 79.85 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.79 | 81.47 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.71 | 81.36 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 82.83 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.81 | 82.15 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
