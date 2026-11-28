' EXPECTED:
' 111
' -1
' 222
' 0
' -1

' comparison > NOT > AND > OR > XOR
X = 4
IF NOT X = 5 THEN > 111
A = -1
B = 0
C = 0
> A OR B AND C
A$ = "a"
B$ = "b"
IF NOT A$ = B$ THEN > 222
> NOT 0 AND 0
> 0 XOR -1 OR 0
