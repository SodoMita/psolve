# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 13:10:27  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.68 ms | 89.46 ms | 33.4x |
| Global Constraints | 16 | 16 | 100.0% | 5.09 ms | 91.18 ms | 17.9x |
| Other | 4 | 4 | 100.0% | 2.71 ms | 1321.58 ms | 488.1x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.78 ms | 87.84 ms | 31.7x |
| Industrial Scheduling | 4 | 4 | 100.0% | 22.48 ms | 93.88 ms | 4.2x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 3.01 ms | 88.60 ms | 29.4x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.87 ms | 98.46 ms | 34.3x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 5.27 ms | 160.73 ms | 30.5x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.87 | 99.03 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 3.07 | 98.57 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.85 | 90.50 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.77 | 89.17 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 81.15 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.90 | 89.23 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 87.92 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 108.40 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.90 | 86.42 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.88 | 87.19 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.91 | 89.28 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 106.63 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 88.83 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 3.10 | 87.61 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 90.62 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 84.17 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 91.19 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.94 | 87.65 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.79 | 79.40 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.87 | 88.37 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 89.99 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.88 | 82.85 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.06 | 91.19 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.99 | 86.62 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.98 | 87.98 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.73 | 89.98 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.10 | 89.67 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.80 | 86.04 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 88.77 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 48.00 | 86.33 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 4.00 | 89.51 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 97.12 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.97 | 91.66 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.92 | 91.17 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 90.56 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 6.00 | 108.85 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 3.44 | 91.54 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.81 | 88.14 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.14 | 87.11 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 83.43 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 86.25 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.72 | 102.97 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.80 | 98.77 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.18 | 90.01 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 6.00 | 93.78 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 3.53 | 111.19 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 3.06 | 90.37 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.85 | 93.33 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.89 | 86.16 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.78 | 84.84 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 84.35 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 88.12 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.00 | 80.77 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 88.64 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.78 | 79.69 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 2.82 | 85.93 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.85 | 87.48 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.85 | 80.73 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.02 | 84.86 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.71 | 78.55 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 3.21 | 87.71 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.89 | 116.25 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 89.89 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.37 | 92.69 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 100.94 | PASS |
| `table.mzn` | Other | SAT | 3 | 5.98 | 89.20 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.85 | 85.40 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.99 | 91.88 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 90.26 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 18.63 | 191.55 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.61 | 151.47 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.09 | 163.89 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.66 | 154.16 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.62 | 147.05 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 156.26 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.79 | 180.63 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
