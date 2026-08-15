# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 13:59:02  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.58 ms | 72.22 ms | 45.7x |
| Global Constraints | 16 | 16 | 100.0% | 4.39 ms | 73.35 ms | 16.7x |
| Other | 4 | 4 | 100.0% | 1.31 ms | 1306.12 ms | 995.1x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.66 ms | 73.00 ms | 44.1x |
| Industrial Scheduling | 4 | 4 | 100.0% | 21.91 ms | 71.50 ms | 3.3x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.67 ms | 72.30 ms | 43.2x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.08 ms | 73.27 ms | 35.3x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 2.59 ms | 88.61 ms | 34.2x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.67 | 72.04 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.68 | 71.47 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.64 | 73.32 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.66 | 73.08 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 73.76 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 1.75 | 71.34 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 98.27 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 71.06 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.67 | 83.98 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.59 | 78.99 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.63 | 71.06 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 72.71 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 70.19 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.86 | 69.70 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 69.51 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 70.65 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 69.90 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 1.56 | 69.21 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.61 | 76.08 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.64 | 69.90 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 70.12 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.57 | 70.70 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.60 | 73.46 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.79 | 70.48 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.63 | 72.96 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 1.61 | 70.41 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.82 | 74.23 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.61 | 85.55 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 73.66 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 46.00 | 70.56 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 76.89 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 82.25 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.55 | 70.66 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.69 | 73.68 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.84 | 72.00 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 69.59 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.66 | 69.74 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.61 | 70.00 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.70 | 70.87 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 70.54 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 70.99 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.55 | 69.73 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.60 | 68.75 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.78 | 70.35 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 72.84 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.34 | 73.63 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.89 | 71.04 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.61 | 70.81 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.65 | 72.18 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.54 | 73.52 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.75 | 72.61 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 70.85 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 69.18 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 77.00 | 71.67 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.58 | 74.07 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.57 | 69.58 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.69 | 70.28 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.54 | 69.03 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.64 | 72.75 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.53 | 70.67 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 1.78 | 74.25 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.00 | 73.92 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 74.92 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.75 | 70.24 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 71.69 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.61 | 78.46 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.55 | 70.53 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.23 | 73.39 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 75.69 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.06 | 84.26 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.58 | 87.00 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.63 | 79.02 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.65 | 103.14 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.63 | 98.03 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.00 | 80.24 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.75 | 78.42 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
