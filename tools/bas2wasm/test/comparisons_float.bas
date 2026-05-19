' EXPECTED:
' 0 -1 -1 0 -1 -1

' Float comparisons
X# = 1.5
Y# = 2.5
A = X# = Y#
B = X# <> Y#
C = X# < Y#
D = X# > Y#
E = X# <= 1.5
F = X# >= 1.5
FORMAT "% % % % % %", A, B, C, D, E, F
