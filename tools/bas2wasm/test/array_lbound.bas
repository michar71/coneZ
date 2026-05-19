' EXPECTED:
' default: lb=1 ub=10
' base 0: lb=0 ub=10
' multi: lb=0 ub1=3 ub2=4

' LBOUND test — default OPTION BASE 1, plus explicit OPTION BASE 0
DIM A(10)
FORMAT "default: lb=% ub=%", LBOUND(A), UBOUND(A)

OPTION BASE 0
DIM B(10)
FORMAT "base 0: lb=% ub=%", LBOUND(B), UBOUND(B)

' UBOUND with dimension index (LBOUND does not take one)
DIM C(3, 4)
FORMAT "multi: lb=% ub1=% ub2=%", LBOUND(C), UBOUND(C, 1), UBOUND(C, 2)
