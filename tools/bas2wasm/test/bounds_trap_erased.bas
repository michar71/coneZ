' EXPECTED:
' 1
' TRAP: unreachable

' An ERASEd array has no storage.
DIM A(3)
A(1) = 1
> A(1)
ERASE A
A(1) = 2
> 999
