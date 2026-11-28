' EXPECTED:
' one
' two
' other
' yes
' no
' 1 2 3
' 6

' QuickBASIC forms: block IF ... THEN, ELSE IF ... THEN, single-line
' IF ... THEN ... ELSE ..., NEXT with the loop variable, DIM with a list.
FOR X = 1 TO 3
  IF X = 1 THEN
    > "one"
  ELSE IF X = 2 THEN
    > "two"
  ELSE
    > "other"
  END IF
NEXT X
IF X > 3 THEN > "yes" ELSE > "no"
IF X < 3 THEN > "yes" ELSE > "no"
DIM A(3), B(3)
FOR I = 1 TO 3
  A(I) = I
  B(I) = A(I) * 2
NEXT I
FORMAT "% % %", A(1), A(2), A(3)
> B(3)
