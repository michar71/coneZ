' EXPECTED:
' 5
' TRAP: unreachable

' Each dimension is checked on its own.
DIM M(2, 3)
M(2, 3) = 5
> M(2, 3)
M(3, 1) = 1
> 999
