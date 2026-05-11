' Regression test: ^ inside a larger expression used to leak vstack entries
' and skip i32→f32 coercion on the outer operand.

' int LHS, ^ on the right
A# = 2 * 3 ^ 2
FORMAT "&", A#

' float LHS, ^ on the right
B# = 1.5 * 2.0 ^ 3.0
FORMAT "&", B#

' ^ on both sides of a binary op
C# = 2.0 ^ 3.0 + 4.0 ^ 0.5
FORMAT "&", C#

' ^ inside an assignment with mixed coercion
D# = (10 - 4) * 2.0 ^ 2.0
FORMAT "&", D#

' ^ followed by integer arithmetic
E# = 2.0 ^ 4.0 - 1.0
FORMAT "&", E#
