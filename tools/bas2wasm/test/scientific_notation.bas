' Float literals with exponent: [eE][+-]?digits, with or without a decimal point
A# = 1e3            ' 1000.0 (no dot)
B# = 1.5e3          ' 1500.0
C# = 1.5E-2         ' 0.015
D# = 2.5E+10        ' 2.5e10
E# = .5e2           ' 50.0
F# = 1.e3           ' 1000.0 (dot without fractional digits)
G# = -1.5e-3        ' -0.0015 (unary minus folds)

FORMAT "& & & & & & &", A#, B#, C#, D#, E#, F#, G#

' Plain identifiers starting with e still work (no backtrack confusion)
END_VAL = 42
E_LATER = 7
FORMAT "% %", END_VAL, E_LATER
