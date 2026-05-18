' Integer comparisons return -1 (true) or 0 (false)

' EXPECTED:
' 0 -1 -1 0 -1 -1
X = 5
Y = 10
A = X = Y
B = X <> Y
C = X < Y
D = X > Y
E = X <= 5
F = X >= 5
FORMAT "% % % % % %", A, B, C, D, E, F
