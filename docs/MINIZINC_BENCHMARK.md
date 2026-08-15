# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 11:04:29  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.66 ms | 78.13 ms | 29.3x |
| Global Constraints | 16 | 16 | 100.0% | 4.92 ms | 85.00 ms | 17.3x |
| Other | 4 | 4 | 100.0% | 1.94 ms | 1308.09 ms | 675.1x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.73 ms | 78.23 ms | 28.6x |
| Industrial Scheduling | 4 | 4 | 100.0% | 19.50 ms | 80.09 ms | 4.1x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.95 ms | 77.38 ms | 26.2x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.74 ms | 83.60 ms | 30.5x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 6.40 ms | 134.48 ms | 21.0x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 3.04 | 77.95 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 4.04 | 150.37 | PASS |
| `alldiff.mzn` | Other | SAT | - | 3.08 | 82.42 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.85 | 79.72 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 78.93 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.93 | 80.87 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 84.01 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 85.96 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.80 | 68.49 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.83 | 76.14 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.98 | 80.47 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 72.79 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.49 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 3.42 | 84.51 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 83.10 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 81.66 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 85.00 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.85 | 78.18 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.92 | 77.29 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 3.15 | 81.04 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 81.54 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.88 | 83.71 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.90 | 79.72 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.98 | 77.61 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.98 | 74.82 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 3.05 | 74.46 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.00 | 79.01 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.91 | 72.78 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 74.95 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 45.00 | 82.46 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 83.31 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 79.83 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.80 | 82.36 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.91 | 77.88 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.90 | 77.51 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 85.93 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.99 | 78.16 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.78 | 82.17 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.94 | 75.67 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 79.58 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 76.03 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.80 | 75.42 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.91 | 75.86 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.02 | 82.30 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 80.27 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.90 | 80.74 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 3.31 | 82.99 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.73 | 85.01 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.88 | 76.43 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.82 | 73.84 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.05 | 82.30 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 75.08 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 84.81 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 66.00 | 79.01 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.95 | 70.80 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 3.16 | 76.95 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.99 | 74.87 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.79 | 77.32 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.05 | 82.34 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.83 | 75.51 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 3.04 | 82.14 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.85 | 76.31 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 77.79 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.96 | 77.11 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 76.03 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.67 | 77.16 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.66 | 78.60 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.84 | 80.62 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 79.73 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 145.73 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 13.32 | 115.89 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 14.32 | 134.94 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.85 | 135.60 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.90 | 136.68 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 4.00 | 138.04 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.07 | 130.14 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
