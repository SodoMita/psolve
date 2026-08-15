# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 22:11:28  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.56 ms | 85.76 ms | 33.5x |
| Global Constraints | 16 | 16 | 100.0% | 5.42 ms | 84.46 ms | 15.6x |
| Other | 4 | 4 | 100.0% | 1.89 ms | 1310.80 ms | 693.5x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.67 ms | 81.54 ms | 30.5x |
| Industrial Scheduling | 4 | 4 | 100.0% | 21.95 ms | 83.03 ms | 3.8x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.89 ms | 101.89 ms | 35.2x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 3.51 ms | 92.68 ms | 26.4x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 8.93 ms | 142.63 ms | 16.0x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.84 | 79.56 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 9.51 | 93.59 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.76 | 79.71 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.77 | 79.69 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 84.27 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.86 | 83.18 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 81.84 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 79.36 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.79 | 76.09 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.69 | 81.64 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.79 | 84.95 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 79.96 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 91.40 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.99 | 81.81 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 82.04 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 80.17 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 87.51 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.78 | 82.44 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.71 | 83.60 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 3.93 | 90.80 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 84.89 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.65 | 80.68 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.76 | 89.83 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.96 | 84.59 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.96 | 131.25 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.94 | 88.52 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.99 | 80.00 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.75 | 79.56 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 79.63 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 49.00 | 84.47 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 86.70 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 79.91 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.61 | 94.05 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.87 | 79.47 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.89 | 81.01 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 80.65 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.83 | 80.19 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.88 | 85.87 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.99 | 88.28 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 98.80 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 84.00 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.72 | 86.86 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.67 | 81.31 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.96 | 81.16 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 6.00 | 84.29 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.72 | 80.46 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.91 | 96.18 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.80 | 79.71 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.80 | 80.30 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.72 | 84.87 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.91 | 88.94 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 4.00 | 86.28 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 79.77 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 76.00 | 86.91 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.83 | 76.61 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 2.82 | 80.59 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.85 | 83.80 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.74 | 80.63 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.84 | 87.34 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.77 | 110.69 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 3.43 | 86.37 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.78 | 78.82 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 85.56 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.79 | 81.27 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 85.96 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.80 | 83.52 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 3.02 | 97.78 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.73 | 81.61 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 84.28 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 12.40 | 149.55 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 14.89 | 110.08 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 11.33 | 157.59 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.91 | 187.18 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 11.03 | 149.36 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 102.04 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 11.03 | 170.45 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
