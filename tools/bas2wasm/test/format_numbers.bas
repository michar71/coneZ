' EXPECTED:
' 16777217.000000
' 1.500000 a
' 7 b
' 2147483647
' -2147483648
' 0

' Integers under & keep full precision; a float under $ prints as a float; a
' float under % saturates instead of trapping (NaN prints 0).
FORMAT "&", 16777217
X# = 1.5
FORMAT "$ $", X#, "a"
FORMAT "$ $", 7, "b"
BIG# = 10000000000.0
FORMAT "%", BIG#
NEG# = -10000000000.0
FORMAT "%", NEG#
Z# = 0.0
N# = Z# / Z#
FORMAT "%", N#
