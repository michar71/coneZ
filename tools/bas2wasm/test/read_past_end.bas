' EXPECTED:
' 3
' TRAP: unreachable

' READ beyond the last DATA item stops the script.
DATA 1, 2
READ A, B
> A + B
READ C
> 999
