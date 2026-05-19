' EXPECTED:
' 256 1.732051 0.01 512

' Power operator ^ — both operands promoted to f32, result is f32
A# = 2.0 ^ 8.0
B# = 3.0 ^ 0.5
C# = 10.0 ^ -2.0
' Right-associative: 2 ^ 3 ^ 2 == 2 ^ (3 ^ 2) == 512
D# = 2.0 ^ 3.0 ^ 2.0
FORMAT "& & & &", A#, B#, C#, D#
