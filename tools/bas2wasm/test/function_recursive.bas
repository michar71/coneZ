' Recursive function — factorial
FUNCTION FACT N
  IF N <= 1 THEN RETURN 1
  RETURN N * FACT(N - 1)
END FUNCTION

FOR I = 0 TO 6
  FORMAT "%! = %", I, FACT(I)
NEXT
