' EXPECTED:
' 2.500000 1.500000
' 7 3

DIM A#(2)
DIM B#(2)
A#(1) = 1.5
B#(1) = 2.5
SWAP A#, B#
FORMAT "& &", A#(1), B#(1)
P = 3
Q = 7
SWAP P, Q
FORMAT "% %", P, Q
