' FOR with STEP, including negative step

' EXPECTED:
' sum=30
' 5! = 120
' ft=2.5
SUM = 0
FOR I = 0 TO 10 STEP 2
  SUM = SUM + I
NEXT
FORMAT "sum=%", SUM

' Negative STEP — count down
PROD = 1
FOR J = 5 TO 1 STEP -1
  PROD = PROD * J
NEXT
FORMAT "5! = %", PROD

' Float FOR loop
FT# = 0.0
FOR K# = 0.0 TO 1.0 STEP 0.25
  FT# = FT# + K#
NEXT
FORMAT "ft=&", FT#
