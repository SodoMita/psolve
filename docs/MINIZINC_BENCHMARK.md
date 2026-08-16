# MiniZinc Benchmark Suite Results
**Date:** 2026-08-16 01:12:59  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.46 ms | 85.31 ms | 34.7x |
| Global Constraints | 16 | 16 | 100.0% | 4.69 ms | 83.89 ms | 17.9x |
| Other | 4 | 4 | 100.0% | 1.72 ms | 1314.84 ms | 764.4x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.60 ms | 83.84 ms | 32.3x |
| Industrial Scheduling | 4 | 4 | 100.0% | 23.68 ms | 86.55 ms | 3.7x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.86 ms | 84.08 ms | 29.4x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 3.48 ms | 97.54 ms | 28.1x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 9.79 ms | 152.74 ms | 15.6x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.63 | 73.92 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.67 | 72.68 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.67 | 73.56 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.63 | 72.95 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 72.02 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.84 | 71.94 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 72.84 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 74.06 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.74 | 83.37 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.67 | 83.37 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.72 | 80.72 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 73.32 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 88.56 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.37 | 77.91 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 105.24 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 100.81 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 88.79 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.91 | 88.54 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 3.04 | 84.46 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.99 | 86.53 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 86.78 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.79 | 84.82 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.79 | 83.98 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 82.47 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.80 | 85.80 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.68 | 82.98 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.12 | 85.19 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.91 | 81.53 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 84.94 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 46.00 | 84.77 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 90.21 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 85.84 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.86 | 97.01 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 3.13 | 82.53 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.08 | 85.02 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 6.00 | 85.18 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.95 | 87.84 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.86 | 81.39 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.97 | 99.01 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 86.08 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 83.67 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.81 | 87.39 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.76 | 88.33 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.04 | 82.21 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 80.99 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.70 | 81.50 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 3.00 | 83.75 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.76 | 81.66 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.87 | 84.23 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.79 | 84.24 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.64 | 105.12 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 113.07 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.00 | 112.19 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 83.00 | 95.37 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.77 | 91.48 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 2.87 | 84.54 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 3.29 | 81.65 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.68 | 84.72 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.83 | 83.01 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.70 | 82.04 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 3.00 | 87.53 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.77 | 82.47 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 88.19 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 88.58 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 92.10 | PASS |
| `table.mzn` | Other | SAT | 3 | 3.21 | 112.47 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.80 | 83.71 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.91 | 84.04 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 84.35 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 13.45 | 151.14 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 14.41 | 160.97 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 15.29 | 193.08 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.71 | 125.53 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 10.86 | 127.82 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 157.88 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 11.36 | 159.24 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
