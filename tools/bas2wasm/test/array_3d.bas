' 3-dimensional integer array — exercises multi-dim index computation
DIM CUBE(2, 3, 4)
CUBE(1, 1, 1) = 111
CUBE(2, 3, 4) = 234
CUBE(1, 2, 3) = 123
FORMAT "% % %", CUBE(1, 1, 1), CUBE(2, 3, 4), CUBE(1, 2, 3)
FORMAT "ndims: % % %", UBOUND(CUBE, 1), UBOUND(CUBE, 2), UBOUND(CUBE, 3)
