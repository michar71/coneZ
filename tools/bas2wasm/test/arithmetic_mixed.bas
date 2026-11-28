' EXPECTED:
' 7.000000 10.000000 2.500000

' Mixed int/float — float wins via promotion
A = 5
B# = 2.0
S# = A + B#
P# = A * B#
Q# = A / B#
FORMAT "& & &", S#, P#, Q#
