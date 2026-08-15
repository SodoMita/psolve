# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 07:44:17  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.49 ms | 63.34 ms | 42.4x |
| Global Constraints | 16 | 16 | 100.0% | 4.21 ms | 63.52 ms | 15.1x |
| Other | 4 | 4 | 100.0% | 1.27 ms | 1296.87 ms | 1019.2x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.55 ms | 63.46 ms | 41.1x |
| Industrial Scheduling | 4 | 4 | 100.0% | 18.63 ms | 63.78 ms | 3.4x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.62 ms | 62.49 ms | 38.7x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.17 ms | 64.81 ms | 29.8x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.60 ms | 71.50 ms | 27.5x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.47 | 62.23 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.59 | 62.19 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.54 | 62.37 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.57 | 62.33 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 63.71 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.59 | 63.46 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 62.87 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 63.19 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.58 | 65.26 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.53 | 62.73 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.54 | 62.93 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 62.67 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 65.49 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.89 | 62.48 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 63.75 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 63.05 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 63.52 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.52 | 63.17 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.49 | 65.69 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.55 | 64.31 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 62.27 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.51 | 62.69 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.51 | 62.30 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.68 | 62.69 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.66 | 62.48 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.43 | 66.58 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.77 | 62.87 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.50 | 64.18 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 62.26 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 43.00 | 62.78 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 63.85 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 62.74 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.84 | 67.92 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.62 | 67.60 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.70 | 62.67 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 66.79 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.76 | 63.27 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.55 | 62.32 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.58 | 63.88 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 62.37 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 65.61 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.49 | 63.28 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.73 | 67.00 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.72 | 63.19 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 63.19 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.46 | 62.92 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.69 | 62.81 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.56 | 62.29 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.53 | 62.63 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.66 | 63.48 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.66 | 62.45 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 63.83 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 61.89 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 64.00 | 63.12 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.58 | 65.01 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.63 | 62.84 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.60 | 63.37 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.46 | 61.54 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.53 | 64.98 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.52 | 62.08 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.71 | 63.19 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.57 | 63.22 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 64.82 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.04 | 62.80 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 63.06 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.55 | 62.45 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.47 | 61.86 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.53 | 61.87 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 65.81 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 72.33 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.64 | 71.08 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.63 | 73.12 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.70 | 70.88 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.64 | 71.16 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 4.00 | 70.41 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.72 | 73.84 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
