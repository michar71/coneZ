' Multi-dim REDIM PRESERVE: must remap (i,j,...) → flat-offset to account
' for stride changes when any non-final dimension's size changes. Old
' realloc-based implementation only worked for 1D — multi-dim silently
' returned wrong values.

' 2D grow on the second dimension (stride change for first dim)
DIM A(3, 4)
A(1, 1) = 11
A(1, 4) = 14
A(2, 1) = 21
A(2, 4) = 24
A(3, 4) = 34
REDIM PRESERVE A(3, 6)
A(3, 6) = 36
' Expected: A(1,1)=11, A(1,4)=14, A(2,1)=21, A(2,4)=24, A(3,4)=34, A(3,6)=36
> A(1,1)
> A(1,4)
> A(2,1)
> A(2,4)
> A(3,4)
> A(3,6)

' 2D shrink on the second dimension — last columns dropped
DIM B(2, 5)
B(1, 1) = 110
B(1, 5) = 150
B(2, 1) = 210
B(2, 5) = 250
REDIM PRESERVE B(2, 3)
' Expected: B(1,1)=110, B(2,1)=210
> B(1,1)
> B(2,1)
