# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 15:22:42  
**Overall Result:** 76/76 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 11.00 ms | 72.71 ms | 6.6x |
| Global Constraints | 16 | 16 | 100.0% | 10.09 ms | 73.66 ms | 7.3x |
| Other | 3 | 3 | 100.0% | 1.39 ms | 76.66 ms | 55.0x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.11 ms | 74.23 ms | 35.1x |
| Industrial Scheduling | 4 | 4 | 100.0% | 52.17 ms | 76.03 ms | 1.5x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 865.55 ms | 74.52 ms | 0.1x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 158.53 ms | 76.19 ms | 0.5x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.10 ms | 72.89 ms | 34.7x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.61 | 71.93 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.57 | 72.39 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.55 | 73.59 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.55 | 71.63 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 70.97 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 7.00 | 72.78 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 73.28 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 3.00 | 72.90 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.00 | 75.92 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.61 | 73.82 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.70 | 71.54 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 79.94 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.04 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.98 | 69.85 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 72.10 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 77.10 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 78.71 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.65 | 70.76 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.62 | 73.35 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.65 | 70.30 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 2.67 | 71.17 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.60 | 71.54 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 79.56 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2592.00 | 72.81 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.66 | 71.20 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.58 | 71.92 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.91 | 75.21 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.59 | 71.37 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 71.16 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 112.00 | 71.69 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 24.00 | 72.34 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1826.00 | 77.30 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 90.41 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.63 | 71.54 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.78 | 75.21 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 7.00 | 76.28 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.70 | 72.78 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.62 | 71.30 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.74 | 72.90 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 72.06 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 71.32 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.61 | 70.79 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.77 | 74.96 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.77 | 73.37 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 61.00 | 72.96 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.62 | 70.00 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 76.44 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.69 | 73.77 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.72 | 73.82 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.69 | 78.70 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.80 | 88.48 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.95 | 73.84 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 74.51 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 193.00 | 85.15 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.64 | 72.96 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.70 | 70.77 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.69 | 70.90 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.60 | 70.54 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.70 | 71.67 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.67 | 69.91 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 3.02 | 72.23 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.72 | 96.95 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 186.00 | 74.63 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.74 | 72.16 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 72.99 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.63 | 76.45 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.66 | 71.98 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.78 | 79.21 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 1.00 | 75.03 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 72.59 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.56 | 78.41 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.59 | 72.07 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.61 | 69.45 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.86 | 74.31 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 70.48 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.69 | 73.20 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
