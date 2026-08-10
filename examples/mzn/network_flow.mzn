% Min-Cost Network Flow (4 nodes, 5 edges)
var 0..20: e12 :: output_var; % capacity 15, cost 2
var 0..20: e13 :: output_var; % capacity 10, cost 4
var 0..20: e23 :: output_var; % capacity 10, cost 1
var 0..20: e24 :: output_var; % capacity 15, cost 5
var 0..20: e34 :: output_var; % capacity 15, cost 3
% Capacities
constraint e12 <= 15 /\ e13 <= 10 /\ e23 <= 10 /\ e24 <= 15 /\ e34 <= 15;
% Flow conservation: Source node 1 (+20), Sink node 4 (-20)
constraint e12 + e13 = 20;       % node 1
constraint e12 - e23 - e24 = 0;  % node 2
constraint e13 + e23 - e34 = 0;  % node 3
constraint e24 + e34 = 20;       % node 4
solve minimize 2*e12 + 4*e13 + 1*e23 + 5*e24 + 3*e34;
