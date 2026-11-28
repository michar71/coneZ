' EXPECTED:
' 1
' TRAP: unreachable

' Writing one past the upper bound stops the script.
DIM A(3)
A(1) = 1
> A(1)
A(4) = 5
> 999
