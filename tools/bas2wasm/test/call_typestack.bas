' EXPECTED:
' 6
' 4
' n=3
' 4
' x7

' A call's argument types must not be left on the compiler's type stack: the
' operator after the call then saw an argument's type as its left operand.
FUNCTION F A
  RETURN A * 2
END FUNCTION
FUNCTION G$ N
  RETURN STR$(N)
END FUNCTION
X# = 1.5 * F(2)
> X#
N = 1 + LIMIT(5, 0, 3)
> N
A$ = "n=" + STR$(LIMIT(5, 0, 3))
> A$
P = 1 + INSTR("hello", "l")
> P
B$ = "x" + G$(7)
> B$
