' EXPECTED:
' 0 0 0

' Smoke test for statement-form builtins. Confirms vstack stays clean
' through SETLEDCOL/WAIT/USEGAMMA when chained inside an expression
' (where vsp doesn't reset between calls).

' Many calls in one expression — the leaky pattern in these builtins
' used to grow vsp by (args + 1) per call. After the cleanup pass each
' should be net +1 to vsp (just the result), so this whole expression
' should fit well within the 64-slot vsp budget.
X = SETLEDCOL(1, 0, 0) + SETLEDCOL(2, 0, 0) + SETLEDCOL(3, 0, 0) + SETLEDCOL(4, 0, 0) + SETLEDCOL(5, 0, 0) + SETLEDCOL(6, 0, 0) + SETLEDCOL(7, 0, 0) + SETLEDCOL(8, 0, 0)
Y = WAIT(1) + WAIT(2) + WAIT(3) + WAIT(4) + WAIT(5) + WAIT(6) + WAIT(7) + WAIT(8)
Z = USEGAMMA(0) + USEGAMMA(0) + USEGAMMA(0) + USEGAMMA(0) + USEGAMMA(0) + USEGAMMA(0)

FORMAT "% % %", X, Y, Z
