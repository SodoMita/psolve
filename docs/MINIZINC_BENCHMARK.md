# MiniZinc Benchmark Suite Results
**Date:** 2026-08-18 16:04:55  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.59 ms | 83.42 ms | 32.3x |
| Global Constraints | 16 | 16 | 100.0% | 4.77 ms | 80.20 ms | 16.8x |
| Other | 4 | 4 | 100.0% | 1.86 ms | 1309.86 ms | 703.3x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.67 ms | 81.74 ms | 30.6x |
| Industrial Scheduling | 4 | 4 | 100.0% | 22.20 ms | 83.00 ms | 3.7x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.86 ms | 81.39 ms | 28.5x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 3.48 ms | 87.18 ms | 25.0x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 8.76 ms | 129.99 ms | 14.8x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.75 | 101.65 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 2.77 | 82.99 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.67 | 79.46 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.62 | 84.02 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 82.12 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.81 | 78.43 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 79.28 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 83.81 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.71 | 72.31 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.63 | 78.64 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.78 | 83.24 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 75.44 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 79.14 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 76.21 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 77.50 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 79.60 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 77.46 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.70 | 82.95 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.74 | 72.92 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.72 | 77.69 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 79.33 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.67 | 84.34 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.73 | 81.00 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.03 | 83.71 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.81 | 79.47 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.73 | 78.32 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.11 | 87.87 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.76 | 74.01 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 74.51 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 45.00 | 77.44 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 4.00 | 78.44 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 78.77 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.64 | 77.78 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.72 | 78.60 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.95 | 92.11 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 81.01 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.82 | 83.40 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.73 | 78.77 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.90 | 83.89 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 77.82 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 77.70 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.70 | 84.28 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.77 | 91.46 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.04 | 84.80 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 79.02 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.68 | 78.66 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 3.01 | 79.45 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.70 | 77.95 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.75 | 77.76 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.75 | 81.88 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.91 | 84.44 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 80.04 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 76.90 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 93.23 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 3.01 | 104.00 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 3.14 | 85.93 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.91 | 87.44 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.66 | 81.87 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.89 | 82.51 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.81 | 88.90 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 2.99 | 85.62 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.80 | 83.99 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.00 | 88.71 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.77 | 85.76 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 80.48 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.78 | 84.52 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.73 | 80.29 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.74 | 82.54 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 84.16 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.04 | 142.79 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 14.40 | 103.78 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 11.90 | 153.35 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 10.87 | 155.78 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 10.35 | 131.67 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 92.56 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 12.67 | 144.55 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
