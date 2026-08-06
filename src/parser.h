#ifndef LP_PARSER_H
#define LP_PARSER_H
#include "solver.h"
/* Parse a simple LP file and fill an LP struct.
 * Returns 0 on success, -1 on error (message printed to stderr). */
int lp_read(const char *path, LP *lp);
void lp_free(LP *lp);
#endif
