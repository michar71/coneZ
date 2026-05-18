' `>` is shorthand for "print this expression with a newline"

' EXPECTED:
' 42
' 3.14
' hello
' 1
' 2
' 3
' 10
' regular print
' format print: 10
> 42
> 3.14
> "hello"

' Inside loops and conditionals
FOR I = 1 TO 3
  > I
NEXT

X = 10
IF X > 5 THEN > X

' Mixed with FORMAT
> "regular print"
FORMAT "format print: %", X
