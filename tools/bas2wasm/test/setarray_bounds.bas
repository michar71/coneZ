' EXPECTED:
' 7 7 7 7 7 ub=5
' 0 9 9 0 0 ub=5
' 4 4 4 4 4 ub=5

' SETARRAY writes elements start..end and leaves the array header alone; an
' out-of-range start/end is clamped to the array's bounds.
DIM A(5)
S = SETARRAY(A, 1, 5, 7)
FORMAT "% % % % % ub=%", A(1), A(2), A(3), A(4), A(5), UBOUND(A)
DIM B(5)
S = SETARRAY(B, 2, 3, 9)
FORMAT "% % % % % ub=%", B(1), B(2), B(3), B(4), B(5), UBOUND(B)
S = SETARRAY(B, -3, 99, 4)
FORMAT "% % % % % ub=%", B(1), B(2), B(3), B(4), B(5), UBOUND(B)
