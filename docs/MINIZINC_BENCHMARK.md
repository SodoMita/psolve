# MiniZinc Benchmark Suite Results
**Date:** 2026-08-11 21:39:37  
**Overall Result:** 77/77 instances passed (100.0%)  

## Overview
psolve includes an exact and integer FlatZinc solver (`fznsolve`) capable of executing diverse MiniZinc models.
This benchmark evaluates accuracy, status honesty, and solve latency across 7 core problem families.

### Summary by Category
| Category | Total | Passed | Pass Rate | Avg psolve (ms) | Avg Ref (ms) | Speedup vs Ref |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| Extensional, Structure & Logic | 20 | 20 | 100.0% | 12.48 ms | 248.51 ms | 19.9x |
| Global Constraints | 16 | 16 | 100.0% | 33.44 ms | 247.07 ms | 7.4x |
| Other | 4 | 4 | 100.0% | 9.41 ms | 1433.05 ms | 152.4x |
| Operations Research & LP/MIP | 12 | 12 | 100.0% | 16.09 ms | 246.00 ms | 15.3x |
| Industrial Scheduling | 4 | 4 | 100.0% | 159.79 ms | 239.74 ms | 1.5x |
| All-Solution Enumeration (-a) | 3 | 3 | 100.0% | 7.75 ms | 244.21 ms | 31.5x |
| Combinatorial Puzzles & CSP | 12 | 12 | 100.0% | 469.40 ms | 255.90 ms | 0.5x |
| Infeasibility Proofs (UNSAT) | 6 | 6 | 100.0% | 14.91 ms | 275.32 ms | 18.5x |

## Detailed Results
| Model | Category | Status | Objective | psolve (ms) | Ref (ms) | Verdict |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `abs.mzn` | Extensional, Structure & Logic | SAT | 5 | 13.64 | 252.13 | PASS |
| `all_equal_demo.mzn` | Global Constraints | SAT | - | 18.46 | 246.55 | PASS |
| `alldiff.mzn` | Other | SAT | - | 15.11 | 238.58 | PASS |
| `alldiff_demo.mzn` | Global Constraints | SAT | - | 17.16 | 247.85 | PASS |
| `alldiff_except_0_demo.mzn` | Global Constraints | SAT | 8 | 2.00 | 242.05 | PASS |
| `assignment.mzn` | Operations Research & LP/MIP | SAT | 13 | 22.00 | 237.07 | PASS |
| `bin_packing_demo.mzn` | Global Constraints | SAT | - | 1.00 | 238.00 | PASS |
| `bin_packing_load_demo.mzn` | Global Constraints | SAT | - | 11.00 | 255.30 | PASS |
| `blending.mzn` | Operations Research & LP/MIP | SAT | 4071 | 13.03 | 237.64 | PASS |
| `bool_and_sat.mzn` | Extensional, Structure & Logic | SAT | - | 17.37 | 244.05 | PASS |
| `bridge_scheduling.mzn` | Industrial Scheduling | SAT | 22 | 17.17 | 235.30 | PASS |
| `circuit.mzn` | Other | SAT | - | 1.00 | 241.34 | PASS |
| `circuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 2.00 | 241.73 | PASS |
| `count.mzn` | Extensional, Structure & Logic | SAT | - | 18.83 | 235.76 | PASS |
| `cumulative_demo.mzn` | Global Constraints | SAT | - | 1.00 | 245.96 | PASS |
| `cumulative_opt.mzn` | Global Constraints | SAT | 7 | 20.00 | 237.20 | PASS |
| `cutting_stock.mzn` | Operations Research & LP/MIP | SAT | 3 | 1.00 | 253.87 | PASS |
| `decreasing_demo.mzn` | Global Constraints | SAT | 23 | 18.46 | 276.21 | PASS |
| `diet.mzn` | Operations Research & LP/MIP | SAT | 10.25 | 17.27 | 289.48 | PASS |
| `diffn_demo.mzn` | Global Constraints | SAT | - | 9.17 | 243.89 | PASS |
| `disjunctive_demo.mzn` | Global Constraints | SAT | 9 | 1.00 | 230.19 | PASS |
| `element.mzn` | Extensional, Structure & Logic | SAT | 40 | 2.62 | 221.84 | PASS |
| `enum_permutations_3.mzn` | All-Solution Enumeration (-a) | SAT | - | 7.00 | 251.80 | PASS |
| `enum_queens_4.mzn` | All-Solution Enumeration (-a) | SAT | - | 5.70 | 236.80 | PASS |
| `enum_subsets.mzn` | All-Solution Enumeration (-a) | SAT | - | 10.55 | 244.03 | PASS |
| `eq_linear.mzn` | Extensional, Structure & Logic | SAT | - | 12.26 | 237.50 | PASS |
| `facility_loc.mzn` | Operations Research & LP/MIP | SAT | 190 | 21.14 | 235.93 | PASS |
| `float_lin.mzn` | Extensional, Structure & Logic | SAT | 29 | 16.67 | 234.49 | PASS |
| `flowshop_3x3.mzn` | Industrial Scheduling | SAT | 9 | 7.00 | 236.07 | PASS |
| `global_cardinality_demo.mzn` | Global Constraints | SAT | - | 327.00 | 241.55 | PASS |
| `global_cardinality_low_up_demo.mzn` | Global Constraints | SAT | - | 72.00 | 248.57 | PASS |
| `golomb_ruler_4.mzn` | Combinatorial Puzzles & CSP | SAT | 6 | 5503.00 | 260.25 | PASS |
| `graph_coloring.mzn` | Combinatorial Puzzles & CSP | SAT | - | 16.35 | 252.08 | PASS |
| `increasing_demo.mzn` | Global Constraints | SAT | 40 | 18.63 | 252.28 | PASS |
| `inverse_demo.mzn` | Extensional, Structure & Logic | SAT | - | 17.17 | 260.37 | PASS |
| `jobshop_3x3.mzn` | Industrial Scheduling | SAT | 14 | 35.00 | 211.77 | PASS |
| `knap_lin.mzn` | Operations Research & LP/MIP | SAT | 10 | 18.61 | 246.45 | PASS |
| `knap_unbounded.mzn` | Operations Research & LP/MIP | SAT | 21 | 13.15 | 247.89 | PASS |
| `latin_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.11 | 239.88 | PASS |
| `lex_less_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 242.78 | PASS |
| `lex_lesseq_demo.mzn` | Global Constraints | SAT | 4 | 1.00 | 256.57 | PASS |
| `lin_ge.mzn` | Extensional, Structure & Logic | SAT | 10 | 17.27 | 264.15 | PASS |
| `lin_ne.mzn` | Extensional, Structure & Logic | SAT | 10 | 17.16 | 249.33 | PASS |
| `magic_square_3.mzn` | Combinatorial Puzzles & CSP | SAT | - | 16.95 | 247.33 | PASS |
| `magic_square_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 13.00 | 245.49 | PASS |
| `max2.mzn` | Extensional, Structure & Logic | SAT | 10 | 15.00 | 246.52 | PASS |
| `member_demo.mzn` | Extensional, Structure & Logic | SAT | 7 | 2.00 | 241.98 | PASS |
| `mix_prod.mzn` | Operations Research & LP/MIP | SAT | 10 | 15.94 | 237.35 | PASS |
| `network_flow.mzn` | Operations Research & LP/MIP | SAT | 130 | 18.51 | 238.56 | PASS |
| `not_eq.mzn` | Extensional, Structure & Logic | SAT | 10 | 13.15 | 232.00 | PASS |
| `nqueens_4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 8.40 | 254.08 | PASS |
| `nqueens_8.mzn` | Combinatorial Puzzles & CSP | SAT | - | 3.00 | 247.88 | PASS |
| `nvalue_demo.mzn` | Extensional, Structure & Logic | SAT | - | 11.00 | 247.69 | PASS |
| `open_shop_3x3.mzn` | Industrial Scheduling | SAT | 6 | 580.00 | 275.81 | PASS |
| `portfolio.mzn` | Operations Research & LP/MIP | SAT | 0.095 | 16.72 | 235.91 | PASS |
| `prod3.mzn` | Extensional, Structure & Logic | SAT | 57 | 18.66 | 350.40 | PASS |
| `prodplan.mzn` | Operations Research & LP/MIP | SAT | 263 | 17.20 | 241.39 | PASS |
| `reif_eq.mzn` | Extensional, Structure & Logic | SAT | 6 | 14.24 | 245.71 | PASS |
| `send_more_money.mzn` | Combinatorial Puzzles & CSP | SAT | - | 17.21 | 255.82 | PASS |
| `setdom.mzn` | Extensional, Structure & Logic | SAT | 5 | 9.14 | 233.83 | PASS |
| `sliding_sum_demo.mzn` | Extensional, Structure & Logic | SAT | 21 | 17.18 | 261.52 | PASS |
| `strictly_increasing_demo.mzn` | Global Constraints | SAT | 29 | 16.13 | 248.22 | PASS |
| `subcircuit_demo.mzn` | Extensional, Structure & Logic | SAT | - | 1.00 | 227.82 | PASS |
| `sudoku_4x4.mzn` | Combinatorial Puzzles & CSP | SAT | - | 13.16 | 252.28 | PASS |
| `sudoku_9x9.mzn` | Combinatorial Puzzles & CSP | SAT | - | 1.00 | 275.86 | PASS |
| `table.mzn` | Other | SAT | 3 | 20.51 | 252.27 | PASS |
| `table_demo.mzn` | Extensional, Structure & Logic | SAT | 3 | 13.20 | 241.31 | PASS |
| `transport.mzn` | Operations Research & LP/MIP | SAT | 190 | 18.52 | 250.50 | PASS |
| `tsp_5.mzn` | Combinatorial Puzzles & CSP | SAT | 34 | 2.00 | 259.80 | PASS |
| `unbounded_product.mzn` | Other | SAT | - | 1.00 | 5000.00 | PASS |
| `unsat_bin_packing.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 1.00 | 273.44 | PASS |
| `unsat_bounds.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 16.62 | 276.04 | PASS |
| `unsat_cumulative.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.13 | 275.39 | PASS |
| `unsat_knapsack.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 20.99 | 292.83 | PASS |
| `unsat_pigeonhole.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 16.74 | 283.08 | PASS |
| `unsat_sudoku.mzn` | Infeasibility Proofs (UNSAT) | UNSAT | - | 17.00 | 251.15 | PASS |
| `zebra.mzn` | Combinatorial Puzzles & CSP | SAT | - | 21.68 | 280.05 | PASS |

## Verification Invariants
- **Mathematical Exactness:** Integer solutions are certified lattice points; float solutions respect bound invariants.
- **Status Honesty:** No false `OPTIMAL` without proof; unbounded/infeasible outside synthetic box reported as `UNKNOWN`.
- **Global Constraints:** Natively linearizes AllDifferent, AllEqual, Lexicographical, GCC, BinPacking, Disjunctive, Inverse, Diffn, Cumulative, Table, Circuit, Subcircuit, SlidingSum, and NValue.
