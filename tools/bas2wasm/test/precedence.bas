' Operator precedence: ^ > unary - > * / \ MOD > + - > comparisons > NOT > AND > OR > XOR

' EXPECTED:
' 14 20 18 4 -1 0 4 3
A = 2 + 3 * 4
B = (2 + 3) * 4
C# = 2 * 3 ^ 2           ' ^ binds tighter than *; result f32 = 18.0
D# = -2.0 ^ 2.0          ' unary minus on f32 const, then ^; powf(-2,2) = 4.0
E = 5 > 3 AND 1 < 2
F = NOT (5 = 5)
G = 7 \ 2 + 1            ' integer divide tighter than +
H = 10 - 5 - 2           ' left-assoc subtraction
FORMAT "% % & & % % % %", A, B, C#, D#, E, F, G, H
