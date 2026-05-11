' DATA / READ / RESTORE
DATA 10, 20, 30
READ A, B, C
FORMAT "% % %", A, B, C

' Mixed-type DATA
DATA 42, 3.14, "hello"
READ I, F#, S$
FORMAT "% & $", I, F#, S$

' RESTORE
RESTORE
READ X
FORMAT "%", X

' Negative numbers
DATA -1, -2.5
READ N1, N2#
FORMAT "% &", N1, N2#
