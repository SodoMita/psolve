# MiniZinc Benchmark Suite Results
**Date:** 2026-08-19 16:39:07  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 1.72 ms | 72.85 ms | 42.3x |
| Global Constraints | 16 | 16 | 100.0% | 4.68 ms | 73.30 ms | 15.7x |
| Other | 4 | 4 | 100.0% | 1.37 ms | 1305.36 ms | 952.8x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 1.80 ms | 73.98 ms | 41.2x |
| Industrial Scheduling | 4 | 4 | 100.0% | 20.67 ms | 72.55 ms | 3.5x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1.76 ms | 72.81 ms | 41.4x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.39 ms | 73.52 ms | 30.8x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 3.31 ms | 82.33 ms | 24.8x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.61 | 69.75 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 1.79 | 70.04 | PASS |
| `alldiff.mzn` | Other | SAT | - | 1.79 | 70.69 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 1.65 | 71.57 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 76.78 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 3.08 | 74.05 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 70.08 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 74.44 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 1.68 | 73.65 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 1.66 | 72.84 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 1.67 | 70.92 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 78.82 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.84 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 1.88 | 71.48 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 81.53 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 3.00 | 77.95 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 78.82 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 3.28 | 72.02 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 1.73 | 84.53 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 1.86 | 72.15 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 71.26 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 1.80 | 75.34 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.66 | 72.46 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.87 | 71.73 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 1.75 | 74.24 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.96 | 76.03 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.89 | 72.78 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 1.65 | 76.23 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 74.60 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 49.00 | 73.26 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 73.06 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 75.25 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.34 | 75.32 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 1.72 | 71.07 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.01 | 71.67 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 71.37 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.79 | 72.06 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 1.65 | 70.73 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.74 | 74.98 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 73.66 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 71.20 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.63 | 70.88 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.63 | 71.11 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.91 | 71.26 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 72.01 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.78 | 74.00 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 1.86 | 78.36 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 1.64 | 70.35 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 1.88 | 73.07 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 1.81 | 70.87 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.96 | 71.20 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 4.00 | 71.50 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 70.78 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 72.00 | 73.31 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 1.65 | 73.81 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 1.72 | 71.28 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 1.86 | 72.27 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 1.74 | 70.82 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.06 | 71.35 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 1.57 | 74.66 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 2.18 | 71.42 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 1.62 | 72.71 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.11 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.78 | 70.74 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 75.83 | PASS |
| `table.mzn` | Other | SAT | 3 | 1.69 | 71.93 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 1.96 | 72.55 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 1.70 | 71.60 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 73.41 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 3.01 | 85.46 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.69 | 79.80 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.72 | 77.99 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.88 | 84.88 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 7.59 | 85.30 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 80.53 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.89 | 79.40 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
