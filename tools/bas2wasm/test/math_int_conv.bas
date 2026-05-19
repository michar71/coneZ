' EXPECTED:
' 5 3.5 -1 0 1 -1
' 3 -3 42

' Integer/float conversion + ABS + SGN
A = ABS(-5)
B# = ABS(-3.5)
S1 = SGN(-7)
S2 = SGN(0)
S3 = SGN(42)
S4 = SGN(-2.5)

I = INT(3.7)            ' f32 → i32 truncate toward zero
IN = INT(-3.7)
F# = FLOAT(42)          ' i32 → f32
FORMAT "% & % % % %", A, B#, S1, S2, S3, S4
FORMAT "% % &", I, IN, F#
