#include "fzn.h"
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char**argv)
{
    if(argc<2){fprintf(stderr,"usage: %s <problem.fzn>\n",argv[0]);return 1;}
    if(setjmp(psolve_env)!=0){fprintf(stderr,"fznsolve: %s\n",psolve_code==PSOLVE_ERR_OOM?"out of memory":"internal error");return 2;}
    psolve_try();
    FZModel m; 
    if(fz_read(argv[1],&m)!=0){psolve_end();return 1;}
    FZSolution sol;
    fz_solve(&m,&sol);
    fz_print_solution(&m,&sol);
    printf("%%%%mzn-stat: intVariables=%d\n", sol.nvars);
    printf("%%%%mzn-stat: solveTime=%.3f\n", 0.0);
    printf("%%%%mzn-stat-end\n");
    fz_solution_free(&sol);
    fz_model_free(&m);
    psolve_end();
    return 0;
}
