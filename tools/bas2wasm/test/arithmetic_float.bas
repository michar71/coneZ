' EXPECTED:
' 5.000000 2.000000 5.250000 2.333333 -3.500000

' Float arithmetic using # suffix
A# = 3.5
B# = 1.5
S# = A# + B#
D# = A# - B#
M# = A# * B#
Q# = A# / B#
N# = -A#
FORMAT "& & & & &", S#, D#, M#, Q#, N#
