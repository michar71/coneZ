' EXPECTED:
' 1
' TRAP: unreachable

' A string variable cannot READ a numeric item.
DATA 5
> 1
READ S$
> 999
