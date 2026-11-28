' EXPECTED:
' 3
' 1
' 2
' 9

' Assignment coerces to the variable's type (it used to store the raw value:
' an f32 into an i32 global -> invalid module). A variable that is READ before
' anything typed it is an integer from then on.
X = 42
X = 3.14
> X
Y# = 1
> Y#
SUM = SUM + 1.5
SUM = SUM + 1.5
> SUM
FOR I = 1 TO 3
NEXT
I = 9.75
> I
