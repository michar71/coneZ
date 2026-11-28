' EXPECTED:
' 7 1.500000 s 2.250000 9
' 1.500000 7
' 0.500000 1.500000 2.500000
' x 3.250000
' 4 5 6.750000

' Argument layout check for FORMAT with floats. host_printf reads a float as a
' DOUBLE at an 8-byte-aligned offset (C varargs), so a float after an odd
' number of int/string arguments has a 4-byte gap before it. Covers float
' after 1 and 2 four-byte cells, float first, consecutive floats, and an int
' after a float.
X = 7
F = 1.5
FORMAT "% & $ & %", X, F, "s", 2.25, 9
FORMAT "& %", F, X
FORMAT "& & &", 0.5, F, 2.5
FORMAT "$ &", "x", 3.25
FORMAT "% % &", 4, 5, 6.75
