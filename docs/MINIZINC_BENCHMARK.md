# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 08:49:05  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.53 ms | 76.87 ms | 30.4x |
| Global Constraints | 16 | 16 | 100.0% | 4.80 ms | 78.02 ms | 16.3x |
| Other | 4 | 4 | 100.0% | 1.92 ms | 1305.97 ms | 680.2x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.68 ms | 75.55 ms | 28.1x |
| Industrial Scheduling | 4 | 4 | 100.0% | 18.95 ms | 77.79 ms | 4.1x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.86 ms | 77.01 ms | 27.0x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 3.23 ms | 80.79 ms | 25.0x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 5.66 ms | 121.44 ms | 21.5x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.71 | 88.08 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 2.69 | 72.70 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.99 | 73.64 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.98 | 73.01 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 80.70 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.84 | 71.44 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 78.68 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 74.70 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.97 | 83.86 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.98 | 85.38 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.78 | 77.55 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 72.76 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 71.64 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 3.21 | 76.37 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 73.34 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 80.97 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 77.49 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.72 | 73.68 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.78 | 68.69 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.79 | 79.77 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 78.20 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.62 | 78.24 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.67 | 77.05 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.05 | 81.71 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.85 | 72.28 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.69 | 74.22 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.99 | 79.75 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.71 | 67.39 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 77.74 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 45.00 | 106.92 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 74.12 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 71.64 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.66 | 76.89 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.73 | 72.94 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.92 | 72.00 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 82.34 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.83 | 77.87 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.68 | 73.15 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.88 | 73.45 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 73.31 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 76.51 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.63 | 77.32 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.70 | 75.23 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.21 | 73.82 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 79.57 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.68 | 72.96 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.98 | 74.19 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.82 | 78.70 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 3.04 | 77.63 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.65 | 72.41 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.16 | 73.78 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 76.89 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 84.11 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 64.00 | 73.52 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.74 | 66.93 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 2.78 | 78.75 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.72 | 77.56 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.74 | 74.81 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.13 | 74.77 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.72 | 92.30 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 2.91 | 73.73 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.89 | 78.85 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 74.72 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.85 | 71.58 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 75.40 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.69 | 77.47 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.98 | 73.59 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.80 | 73.59 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 82.28 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 133.79 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.12 | 152.00 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 11.06 | 90.99 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 11.90 | 83.19 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.87 | 134.93 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 4.00 | 133.71 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 8.91 | 139.41 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
