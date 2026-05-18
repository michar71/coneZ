' Recursive function — factorial

' EXPECTED:
' 0! = 1
' 1! = 1
' 2! = 2
' 3! = 6
' 4! = 24
' 5! = 120
' 6! = 720
FUNCTION FACT N
  IF N <= 1 THEN RETURN 1
  RETURN N * FACT(N - 1)
END FUNCTION

FOR I = 0 TO 6
  FORMAT "%! = %", I, FACT(I)
NEXT
