' EXPECTED:
' Hello Earth
' ABXXEF
' ABC3456789
' #ab#######

' MID$ assignment — in-place character replacement (never grows or shrinks)
A$ = "Hello World"
MID$(A$, 7, 5) = "Earth"
FORMAT "$", A$

B$ = "ABCDEF"
MID$(B$, 3, 2) = "XX"
FORMAT "$", B$

C$ = "0123456789"
MID$(C$, 1, 3) = "ABC"
FORMAT "$", C$

' Replacement shorter than length — only as many chars as replacement supplies
D$ = "##########"
MID$(D$, 2, 5) = "ab"
FORMAT "$", D$
