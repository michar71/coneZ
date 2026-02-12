X& = 4294967296&
Y& = &H100000000&
Z& = X& + Y&
IF Z& > X& THEN > Z&

A = &H2A
IF A = 42 THEN > A

B& = 0x100000000
IF B& = X& THEN > STR$(B&)

T& = UPTIME&()
E& = EPOCHMS&()
C& = CUEELAPSED&()
S$ = STR$(T&)
P& = VAL&(S$)
IF P& >= 0 THEN > STR$(P&)
