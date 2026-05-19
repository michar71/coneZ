' EXPECTED:
' 5 2 5.25 2.333333 -3.5

' Float arithmetic using # suffix
A# = 3.5
B# = 1.5
S# = A# + B#
D# = A# - B#
M# = A# * B#
Q# = A# / B#
N# = -A#
FORMAT "& & & & &", S#, D#, M#, Q#, N#
