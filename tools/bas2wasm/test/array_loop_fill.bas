' Loop-driven array fill + sum
DIM A(20)
FOR I = 1 TO 20
  A(I) = I * I
NEXT
SUM = 0
FOR J = 1 TO 20
  SUM = SUM + A(J)
NEXT
FORMAT "sum of squares 1..20 = %", SUM
