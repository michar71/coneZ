' EXPECTED:
' hello hello <>
' abcdefgh
' ABCxyz42
' [one]
' abcdabcd

' String concatenation edge cases

' Empty string on either side
E$ = ""
A$ = E$ + "hello"
B$ = "hello" + E$
C$ = E$ + E$
FORMAT "$ $ <$>", A$, B$, C$

' Long chain of concats
LONG$ = "a" + "b" + "c" + "d" + "e" + "f" + "g" + "h"
FORMAT "$", LONG$

' Concat with function-result strings
F$ = UPPER$("abc") + LOWER$("XYZ") + STR$(42)
FORMAT "$", F$

' Concat involving MID$ on the right-hand side
S$ = "ConeZ"
G$ = "[" + MID$(S$, 2, 3) + "]"
FORMAT "$", G$

' Concat into self
H$ = "ab"
H$ = H$ + "cd"
H$ = H$ + H$
FORMAT "$", H$
