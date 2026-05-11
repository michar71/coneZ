' BASIC AND/OR are bitwise — combined with the -1/0 truth convention they
' double as logical operators. This test pins down the actual semantics.

' Truth table (all four corners)
A = -1 AND -1     ' true AND true = -1
B = -1 AND 0      ' true AND false = 0
C = 0 OR -1       ' false OR true = -1
D = 0 OR 0        ' false OR false = 0
FORMAT "% % % %", A, B, C, D

' Bitwise behavior on non-boolean ints
E = 5 AND 3       ' 0101 AND 0011 = 0001 = 1
F = 5 OR 3        ' 0101 OR 0011 = 0111 = 7
G = NOT 0         ' bitwise NOT of 0 = -1 (all bits set)
H = NOT 5         ' bitwise NOT of 0...0101 = ...11111010 = -6
FORMAT "% % % %", E, F, G, H

' Used as logical: comparison result feeds AND/OR
X = 5
Y = 10
P = (X > 0) AND (Y > 0)
Q = (X > 100) OR (Y > 5)
R = NOT (X = Y)
FORMAT "% % %", P, Q, R

' XOR
I = -1 XOR -1     ' 0
J = -1 XOR 0      ' -1
K = 5 XOR 3       ' 0101 XOR 0011 = 0110 = 6
FORMAT "% % %", I, J, K
