# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 15:27:10  
**Overall Result:** 76/76 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 10.94 ms | 72.30 ms | 6.6x |
| Global Constraints | 16 | 16 | 100.0% | 10.32 ms | 72.93 ms | 7.1x |
| Other | 3 | 3 | 100.0% | 1.51 ms | 73.35 ms | 48.7x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.00 ms | 72.44 ms | 36.2x |
| Industrial Scheduling | 4 | 4 | 100.0% | 53.20 ms | 73.79 ms | 1.4x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 859.05 ms | 72.30 ms | 0.1x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 160.06 ms | 74.75 ms | 0.5x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.11 ms | 73.07 ms | 34.5x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.87 | 74.79 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.71 | 73.81 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.82 | 75.36 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.63 | 74.35 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 76.49 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 6.00 | 71.64 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 71.62 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 3.00 | 71.48 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.71 | 75.52 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.69 | 75.86 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.78 | 72.80 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 72.57 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.93 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.00 | 71.83 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 72.14 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 73.78 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 72.52 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.70 | 71.64 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.69 | 75.05 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.68 | 71.72 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 2.09 | 72.49 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.68 | 70.52 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 71.67 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2572.00 | 73.98 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.14 | 71.24 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.61 | 69.97 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.94 | 73.85 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.67 | 77.62 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 71.60 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 115.00 | 73.95 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 24.00 | 76.08 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1853.00 | 79.62 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 86.68 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.60 | 71.19 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.74 | 73.81 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 8.00 | 73.97 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.76 | 69.85 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.65 | 71.31 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.75 | 70.35 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 70.99 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 2.00 | 75.81 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.55 | 71.00 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.57 | 71.48 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.72 | 72.67 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 52.00 | 72.98 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.56 | 69.42 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 71.13 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.64 | 70.58 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.62 | 73.61 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.54 | 69.89 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.63 | 71.58 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 70.32 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 70.39 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 196.00 | 76.80 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.66 | 73.21 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.62 | 71.48 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.71 | 70.20 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.55 | 69.65 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.23 | 73.15 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.59 | 71.32 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.81 | 71.05 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.64 | 69.28 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 186.00 | 79.04 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.84 | 71.78 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 77.12 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.70 | 72.11 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.68 | 71.76 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.64 | 71.94 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 77.55 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 74.63 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.62 | 70.81 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.63 | 71.53 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.64 | 70.54 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.80 | 71.76 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 79.14 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.61 | 73.20 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
