' EXPECTED:
' 5 one Con neZ CONEZ conez
' 2 0 A 65 9999999999 3.14
' 42 hi hi hi     XXXX FF 10

' String builtins
S$ = "ConeZ"

L = LEN(S$)
M$ = MID$(S$, 2, 3)
LF$ = LEFT$(S$, 3)
RG$ = RIGHT$(S$, 3)
U$ = UPPER$(S$)
LO$ = LOWER$(S$)
UC$ = UCASE$(S$)
LC$ = LCASE$(S$)
P = INSTR(S$, "one")
P2 = INSTR(S$, "z", 3)

CH$ = CHR$(65)
N = ASC("A")
N$ = STR$(42)
V = VAL("123")
V2& = VAL&("9999999999")
V3# = VAL#("3.14")

T$ = TRIM$("  hi  ")
LT$ = LTRIM$("  hi")
RT$ = RTRIM$("hi  ")
SP$ = SPACE$(3)
ST$ = STRING$(4, 88)   ' "XXXX"
HX$ = HEX$(255)
OC$ = OCT$(8)

FORMAT "% $ $ $ $ $", L, M$, LF$, RG$, U$, LO$
FORMAT "% % $ % & &", P, P2, CH$, N, V2&, V3#
FORMAT "$ $ $ $ $ $ $ $", N$, T$, LT$, RT$, SP$, ST$, HX$, OC$
