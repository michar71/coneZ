' EXPECTED:
' 60

' Scratch locals are recycled per statement, so a long run of array statements
' fits in one function (the firmware build allows 64 locals per function).
DIM A(5)
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
A(1) = A(1) + 1
> A(1)
