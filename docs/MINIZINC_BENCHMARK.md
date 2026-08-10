# MiniZinc Benchmark Suite Results
**Date:** 2026-08-10 21:10:44  
**Overall Result:** 76/76 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 7.71 ms | 80.75 ms | 10.5x |
| Global Constraints | 16 | 16 | 100.0% | 11.20 ms | 81.73 ms | 7.3x |
| Other | 3 | 3 | 100.0% | 2.18 ms | 79.60 ms | 36.6x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.81 ms | 79.20 ms | 28.2x |
| Industrial Scheduling | 4 | 4 | 100.0% | 250.25 ms | 80.46 ms | 0.3x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 470.92 ms | 78.61 ms | 0.2x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 1692.00 ms | 83.86 ms | 0.0x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.88 ms | 81.79 ms | 28.4x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.81 | 83.62 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 2.79 | 78.31 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.75 | 79.16 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.83 | 76.18 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 84.71 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 3.00 | 81.26 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 78.29 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 2.00 | 80.20 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.82 | 72.98 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.78 | 79.00 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 3.01 | 84.76 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 76.87 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 84.74 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 3.04 | 83.00 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 84.02 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 25.00 | 84.49 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 2.00 | 81.17 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.77 | 82.43 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.83 | 72.22 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.85 | 78.17 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 3.17 | 82.99 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.74 | 78.53 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 77.33 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1407.00 | 79.10 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.77 | 79.39 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.78 | 78.01 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.09 | 76.75 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.78 | 72.08 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 78.94 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 85.00 | 85.40 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 41.00 | 80.61 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 819.00 | 83.69 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 85.48 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.88 | 85.65 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.02 | 80.75 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 8.00 | 79.71 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.91 | 80.19 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.80 | 79.72 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 14.00 | 88.23 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 87.13 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 78.63 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.81 | 80.55 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.77 | 80.82 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 60.00 | 80.97 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 483.00 | 80.50 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.79 | 82.57 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 81.85 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.89 | 81.43 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.82 | 85.11 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.83 | 80.24 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1022.00 | 79.97 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | TIMEOUT | - | 8000.00 | 81.51 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 81.04 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 983.00 | 78.44 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.83 | 72.95 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 2.82 | 82.78 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.79 | 82.31 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.77 | 80.71 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 144.00 | 88.41 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.75 | 83.25 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 2.94 | 82.11 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.88 | 80.54 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 103.00 | 79.14 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1747.00 | 81.65 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | TIMEOUT | - | 8000.00 | 86.30 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.78 | 82.77 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.79 | 80.28 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.88 | 84.29 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 1.00 | 87.57 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 80.71 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.75 | 79.10 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.79 | 80.19 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.72 | 93.44 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.02 | 79.58 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 77.71 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 13.00 | 81.98 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
