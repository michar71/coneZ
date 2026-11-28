' EXPECTED:
' 7 9
' 5
' 11 22

' In-range accesses at both ends of each dimension, both OPTION BASEs.
DIM A(3)
A(1) = 7
A(3) = 9
FORMAT "% %", A(1), A(3)
DIM M(2, 3)
M(2, 3) = 5
> M(2, 3)
OPTION BASE 0
DIM Z(3)
Z(0) = 11
Z(3) = 22
FORMAT "% %", Z(0), Z(3)
