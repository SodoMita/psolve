# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 17:13:03  
**Overall Result:** 76/76 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.85 ms | 78.80 ms | 42.7x |
| Global Constraints | 16 | 16 | 100.0% | 10.81 ms | 79.22 ms | 7.3x |
| Other | 3 | 3 | 100.0% | 1.87 ms | 94.16 ms | 50.4x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.05 ms | 77.88 ms | 37.9x |
| Industrial Scheduling | 4 | 4 | 100.0% | 248.96 ms | 76.58 ms | 0.3x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 497.58 ms | 81.41 ms | 0.2x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 72.13 ms | 79.44 ms | 1.1x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.14 ms | 79.89 ms | 37.4x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.80 | 76.81 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.80 | 78.84 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.76 | 106.04 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.23 | 85.88 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 3.00 | 86.47 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 4.00 | 77.33 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 75.48 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 2.00 | 79.60 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.78 | 79.39 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.74 | 86.25 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.84 | 75.98 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 76.03 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 76.28 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.04 | 78.12 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 82.06 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 25.00 | 76.36 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 2.00 | 77.25 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.76 | 78.29 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.82 | 80.72 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.74 | 75.33 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 79.32 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.70 | 75.73 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 76.36 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1488.00 | 91.72 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.74 | 76.16 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.71 | 76.19 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.70 | 77.55 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.78 | 86.73 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 78.11 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 86.00 | 75.78 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 41.00 | 76.92 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 841.00 | 77.91 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 74.81 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.72 | 84.02 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.10 | 75.59 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 8.00 | 76.02 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.86 | 76.73 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.70 | 77.21 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.88 | 77.57 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 76.77 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 81.44 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.96 | 76.52 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.70 | 82.56 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.96 | 75.23 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 81.62 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.89 | 78.34 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 77.47 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.70 | 76.66 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.72 | 75.96 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.66 | 78.57 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.87 | 79.65 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 75.48 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 81.35 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 979.00 | 76.20 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.71 | 79.52 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.75 | 75.10 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.86 | 77.59 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.80 | 75.85 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.98 | 80.70 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.68 | 78.89 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.92 | 78.19 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.64 | 74.95 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 83.34 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.81 | 80.91 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 88.33 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.85 | 100.42 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.71 | 78.14 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.78 | 78.61 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 1.00 | 80.48 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 76.92 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.60 | 75.83 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.74 | 80.74 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.78 | 81.62 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.70 | 80.13 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 84.08 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.03 | 80.57 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
