# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 20:18:16  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 5.81 ms | 146.40 ms | 25.2x |
| Global Constraints | 16 | 16 | 100.0% | 13.54 ms | 142.94 ms | 10.6x |
| Other | 4 | 4 | 100.0% | 1.83 ms | 1355.47 ms | 738.7x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 5.61 ms | 148.50 ms | 26.5x |
| Industrial Scheduling | 4 | 4 | 100.0% | 58.13 ms | 150.98 ms | 2.6x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 1171.53 ms | 143.22 ms | 0.1x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 213.40 ms | 151.04 ms | 0.7x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 7.59 ms | 170.09 ms | 22.4x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.72 | 140.00 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 10.05 | 137.44 | PASS |
| `alldiff.mzn` | Other | SAT | - | 2.63 | 147.50 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 2.61 | 141.34 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 143.26 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 6.00 | 159.69 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 138.49 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 3.00 | 144.98 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 2.65 | 132.34 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 2.64 | 160.42 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 8.52 | 147.22 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 119.45 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 136.92 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 2.91 | 144.73 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 144.40 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 149.58 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 152.36 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 9.17 | 137.30 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 12.06 | 149.48 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 18.49 | 163.42 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 124.82 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 12.28 | 145.72 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 157.16 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 3501.00 | 153.66 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 10.58 | 118.85 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 9.66 | 154.07 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 3.00 | 148.91 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 2.70 | 157.59 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 141.25 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 111.00 | 154.77 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 46.00 | 140.40 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 2481.00 | 151.24 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 11.02 | 137.98 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 2.65 | 150.18 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 15.18 | 147.33 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 11.00 | 156.42 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.80 | 159.66 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 2.69 | 153.53 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 13.77 | 143.02 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 134.54 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 140.13 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.63 | 145.20 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 9.37 | 140.09 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 10.37 | 153.60 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 174.37 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.64 | 143.36 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 148.06 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 2.70 | 141.52 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 18.53 | 137.52 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 2.64 | 155.91 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.78 | 121.51 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 147.52 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 132.96 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 206.00 | 159.02 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 10.35 | 159.79 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 8.99 | 153.69 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 2.84 | 157.19 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 2.79 | 150.81 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 2.76 | 150.99 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 2.70 | 155.09 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 10.58 | 131.62 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 2.71 | 142.03 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 164.69 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.61 | 155.97 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 147.53 | PASS |
| `table.mzn` | Other | SAT | 3 | 2.71 | 154.94 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 18.81 | 119.83 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 2.67 | 130.01 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 160.84 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 170.80 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 10.35 | 163.94 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 10.45 | 168.07 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 10.47 | 164.03 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 8.27 | 174.68 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 179.03 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 10.53 | 167.97 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
