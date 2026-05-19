' EXPECTED:
' total=36
' sum=8

' Nested loops — exercises block_depth tracking
TOTAL = 0
FOR I = 1 TO 3
  FOR J = 1 TO 3
    TOTAL = TOTAL + I * J
  NEXT
NEXT
FORMAT "total=%", TOTAL

' Mixed nesting: FOR inside WHILE inside DO
SUM = 0
N = 0
DO
  N = N + 1
  K = 0
  WHILE K < 2
    FOR M = 1 TO 2
      SUM = SUM + 1
    NEXT
    K = K + 1
  WEND
LOOP UNTIL N = 2
FORMAT "sum=%", SUM
