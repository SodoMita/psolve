# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 20:52:18  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 11.53 ms | 204.21 ms | 17.7x |
| Global Constraints | 16 | 16 | 100.0% | 29.14 ms | 200.06 ms | 6.9x |
| Other | 4 | 4 | 100.0% | 5.97 ms | 1411.70 ms | 236.3x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 12.10 ms | 199.19 ms | 16.5x |
| Industrial Scheduling | 4 | 4 | 100.0% | 133.65 ms | 226.10 ms | 1.7x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 2129.49 ms | 185.58 ms | 0.1x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 391.36 ms | 201.49 ms | 0.5x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 11.64 ms | 242.05 ms | 20.8x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 8.72 | 163.91 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 14.56 | 202.00 | PASS |
| `alldiff.mzn` | Other | SAT | - | 13.57 | 224.43 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 14.53 | 184.11 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 228.54 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 7.00 | 199.79 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 167.27 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 3.00 | 200.29 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 14.48 | 244.86 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 14.12 | 236.43 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 14.61 | 238.78 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 210.32 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 215.23 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 18.77 | 251.71 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 210.14 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 211.49 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 217.59 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 10.54 | 222.54 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 15.74 | 203.63 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 11.87 | 170.51 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 206.13 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 21.19 | 191.61 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 168.93 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 6371.00 | 225.21 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 14.47 | 162.59 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 14.40 | 230.33 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 17.17 | 167.40 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 14.50 | 156.01 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 15.00 | 229.41 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 298.00 | 232.43 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 76.00 | 175.33 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 4596.00 | 188.50 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.17 | 216.39 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 14.49 | 191.92 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 16.86 | 232.78 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 23.00 | 196.73 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 19.20 | 227.87 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 10.49 | 164.09 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 12.29 | 155.73 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 201.51 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 183.32 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 9.68 | 165.92 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 14.42 | 222.73 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 12.92 | 166.35 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 5.00 | 193.12 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 12.77 | 184.29 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 215.80 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 11.66 | 250.52 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 10.73 | 184.49 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 15.60 | 252.08 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 8.40 | 167.89 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 323.91 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 203.20 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 482.00 | 239.49 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 10.93 | 180.37 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 13.05 | 179.71 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 12.32 | 162.98 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 10.03 | 175.20 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 14.16 | 194.81 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 10.39 | 215.19 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 18.69 | 190.42 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 12.32 | 213.37 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 181.78 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 8.20 | 167.56 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 203.85 | PASS |
| `table.mzn` | Other | SAT | 3 | 8.33 | 212.06 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 10.49 | 219.86 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 14.46 | 186.73 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 1.00 | 190.28 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 247.90 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 13.11 | 237.36 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.13 | 235.94 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 16.09 | 235.91 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.52 | 243.32 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 5.00 | 251.87 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.14 | 249.48 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
