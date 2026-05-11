' String concatenation
A$ = "Hello"
B$ = " "
C$ = "World"
D$ = A$ + B$ + C$
FORMAT "$", D$

' Concatenation with function results
E$ = LEFT$("ConeZ", 4) + RIGHT$("alpha", 3)
FORMAT "$", E$

' Empty string
EMPTY$ = ""
F$ = EMPTY$ + "non-empty"
FORMAT "$", F$
