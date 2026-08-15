# MiniZinc Benchmark Suite Results
**Date:** 2026-08-15 10:11:58  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 2.68 ms | 78.33 ms | 29.3x |
| Global Constraints | 16 | 16 | 100.0% | 4.75 ms | 77.18 ms | 16.2x |
| Other | 4 | 4 | 100.0% | 1.94 ms | 1307.27 ms | 675.6x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 2.70 ms | 74.50 ms | 27.6x |
| Industrial Scheduling | 4 | 4 | 100.0% | 18.96 ms | 75.60 ms | 4.0x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2.96 ms | 75.55 ms | 25.5x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 2.60 ms | 81.43 ms | 31.3x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 7.68 ms | 118.68 ms | 15.5x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 3.19 | 78.35 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 2.83 | 79.48 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.91 | 81.43 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.93 | 80.77 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 1.00 | 82.05 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 2.88 | 73.94 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 2.00 | 74.00 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 1.00 | 74.00 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.82 | 75.31 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.88 | 74.82 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 2.86 | 73.96 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 76.10 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 73.80 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 3.07 | 79.81 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 77.83 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 74.08 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 74.94 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 2.80 | 75.68 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 2.82 | 68.70 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 2.87 | 78.08 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 78.55 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.73 | 80.18 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.75 | 74.90 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.92 | 77.84 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.21 | 73.92 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 2.76 | 77.43 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.23 | 91.39 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 3.07 | 68.35 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 4.00 | 77.62 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 44.00 | 80.44 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 3.00 | 79.00 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 1.00 | 79.36 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.73 | 73.10 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.84 | 75.86 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.97 | 72.93 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 5.00 | 76.91 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.91 | 71.62 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.80 | 73.67 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.89 | 73.45 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 78.94 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 74.00 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 3.37 | 84.73 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 3.13 | 75.97 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.29 | 92.25 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 76.62 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 3.07 | 123.75 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 3.02 | 79.70 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.74 | 72.11 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 2.69 | 77.58 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.75 | 76.04 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.89 | 71.76 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 72.90 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 75.03 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 64.00 | 73.93 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 2.82 | 68.63 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 3.27 | 74.37 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.84 | 72.26 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.72 | 74.36 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.80 | 74.70 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.76 | 72.85 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 2.96 | 75.75 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.79 | 72.16 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 75.80 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.77 | 73.38 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 78.62 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.83 | 71.55 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 2.84 | 72.63 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.80 | 73.82 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 1.00 | 75.69 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 106.67 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 14.27 | 115.55 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 2.97 | 129.12 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 11.27 | 123.27 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 12.57 | 99.39 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 4.00 | 138.06 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.83 | 135.35 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
