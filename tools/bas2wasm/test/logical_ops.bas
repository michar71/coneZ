' AND / OR / NOT / XOR using BASIC truth values (-1 / 0)

' EXPECTED:
' -1 0 -1 0 0 -1 0 -1
T = -1
F = 0
A = T AND T
B = T AND F
C = T OR F
D = F OR F
E = NOT T
G = NOT F
H = T XOR T
I = T XOR F
FORMAT "% % % % % % % %", A, B, C, D, E, G, H, I
