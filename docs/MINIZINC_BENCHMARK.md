# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 17:01:50  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.62 ms | 76.62 ms | 47.3x |
| Global Constraints | 16 | 16 | 100.0% | 4.37 ms | 71.86 ms | 16.5x |
| Other | 4 | 4 | 100.0% | 1.32 ms | 1306.13 ms | 991.4x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.71 ms | 77.26 ms | 45.3x |
| Industrial Scheduling | 4 | 4 | 100.0% | 21.93 ms | 71.92 ms | 3.3x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.81 ms | 79.59 ms | 44.1x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.11 ms | 76.78 ms | 36.3x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.60 ms | 82.96 ms | 31.8x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.58 | 70.64 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.59 | 71.83 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.55 | 74.39 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.55 | 72.01 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 69.43 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.00 | 82.84 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 69.15 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 70.60 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.87 | 87.08 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.63 | 113.09 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.70 | 73.12 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 76.41 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 85.10 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.30 | 71.85 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 71.89 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 72.47 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 73.48 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.59 | 70.74 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.62 | 86.46 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.77 | 75.32 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 71.19 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.87 | 70.98 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.58 | 70.27 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.05 | 72.62 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.79 | 95.88 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.62 | 82.77 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.97 | 73.25 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.67 | 74.43 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 70.25 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 45.00 | 72.89 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 4.00 | 73.27 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 71.04 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.64 | 72.31 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.75 | 68.33 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.97 | 69.38 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 69.93 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.78 | 73.84 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.71 | 72.82 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.88 | 72.97 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 72.05 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 71.45 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.60 | 74.04 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.72 | 74.12 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.00 | 72.83 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 71.49 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.64 | 73.79 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 70.19 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.63 | 72.83 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.76 | 81.69 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.72 | 73.79 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.84 | 72.97 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 72.56 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 72.89 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 74.36 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.67 | 78.45 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.64 | 70.54 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.67 | 72.00 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.59 | 71.88 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.63 | 72.60 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.51 | 75.89 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.74 | 71.97 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.62 | 77.10 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 86.48 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.70 | 72.78 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 80.39 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.72 | 73.74 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.59 | 78.59 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.79 | 72.41 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 75.40 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.08 | 80.43 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.64 | 86.22 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.61 | 78.38 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.63 | 82.54 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.67 | 85.12 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 85.07 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.69 | 113.98 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
