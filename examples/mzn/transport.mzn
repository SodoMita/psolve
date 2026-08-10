% Transportation problem from 2 plants to 3 markets
array[1..2, 1..3] of var 0..50: flow :: output_array([1..2, 1..3]);
% Plant capacities: 40, 50
constraint sum(j in 1..3)(flow[1, j]) <= 40;
constraint sum(j in 1..3)(flow[2, j]) <= 50;
% Market demands: 25, 35, 20
constraint sum(i in 1..2)(flow[i, 1]) >= 25;
constraint sum(i in 1..2)(flow[i, 2]) >= 35;
constraint sum(i in 1..2)(flow[i, 3]) >= 20;
% Costs: [2, 4, 5], [3, 1, 7]
solve minimize 2*flow[1,1] + 4*flow[1,2] + 5*flow[1,3] + 3*flow[2,1] + 1*flow[2,2] + 7*flow[2,3];
