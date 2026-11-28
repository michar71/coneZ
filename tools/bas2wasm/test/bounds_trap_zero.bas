' EXPECTED:
' 1
' TRAP: unreachable

' Index 0 under the default OPTION BASE 1 is out of range (it used to land on
' the array's own stored upper bound).
DIM A(3)
> 1
X = A(0)
> 999
