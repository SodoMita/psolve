# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 21:25:30  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 12.92 ms | 247.43 ms | 19.2x |
| Global Constraints | 16 | 16 | 100.0% | 31.55 ms | 252.72 ms | 8.0x |
| Other | 4 | 4 | 100.0% | 9.12 ms | 1433.61 ms | 157.2x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 14.80 ms | 249.80 ms | 16.9x |
| Industrial Scheduling | 4 | 4 | 100.0% | 155.63 ms | 251.61 ms | 1.6x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 5.17 ms | 263.39 ms | 50.9x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 476.58 ms | 244.13 ms | 0.5x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 13.91 ms | 264.00 ms | 19.0x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 14.61 | 252.11 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 20.98 | 249.42 | PASS |
| `alldiff.mzn` | Other | SAT | - | 14.41 | 242.64 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 13.80 | 245.40 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 255.84 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 23.00 | 240.11 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 249.74 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 4.00 | 271.54 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 17.18 | 243.86 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 17.17 | 295.90 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 10.53 | 248.21 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 259.86 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 253.59 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 16.59 | 243.76 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 243.93 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 4.00 | 259.77 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 261.81 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 16.46 | 247.89 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 10.67 | 252.13 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 15.71 | 259.84 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 245.30 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 17.18 | 261.70 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 2.00 | 259.82 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 3.00 | 287.79 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 10.52 | 242.57 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 18.55 | 250.63 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 17.22 | 257.30 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 20.53 | 236.04 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 249.32 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 328.00 | 263.02 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 64.00 | 249.37 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 5578.00 | 238.93 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 14.25 | 251.96 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 13.09 | 262.56 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 10.61 | 249.78 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 23.00 | 257.08 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 17.18 | 249.25 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 17.18 | 241.25 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 13.70 | 246.35 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 251.83 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 249.40 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 16.04 | 296.06 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 17.60 | 252.00 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.22 | 243.80 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 13.00 | 237.23 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 13.31 | 255.69 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 238.03 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 9.14 | 249.34 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 17.45 | 238.51 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 17.16 | 241.32 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.17 | 227.90 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 251.90 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 3.00 | 287.58 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 582.00 | 251.83 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 13.16 | 252.39 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 10.57 | 254.49 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 17.33 | 266.32 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 17.13 | 239.90 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 21.37 | 212.24 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 17.05 | 237.18 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 10.61 | 183.83 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 17.75 | 238.62 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 175.05 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.14 | 237.31 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 237.88 | PASS |
| `table.mzn` | Other | SAT | 3 | 20.08 | 231.92 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 16.60 | 243.88 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 17.15 | 245.28 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 255.85 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 260.07 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.05 | 288.04 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.01 | 252.58 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.59 | 291.05 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.81 | 234.73 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 13.00 | 257.56 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 21.12 | 288.25 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
