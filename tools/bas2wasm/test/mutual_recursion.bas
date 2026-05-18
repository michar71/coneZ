' Mutual recursion via DECLARE

' EXPECTED:
' 0 even=-1 odd=0
' 1 even=0 odd=-1
' 2 even=-1 odd=0
' 3 even=0 odd=-1
' 4 even=-1 odd=0
' 5 even=0 odd=-1
DECLARE FUNCTION IS_ODD N

FUNCTION IS_EVEN N
  IF N = 0 THEN RETURN -1
  RETURN IS_ODD(N - 1)
END FUNCTION

FUNCTION IS_ODD N
  IF N = 0 THEN RETURN 0
  RETURN IS_EVEN(N - 1)
END FUNCTION

FOR I = 0 TO 5
  FORMAT "% even=% odd=%", I, IS_EVEN(I), IS_ODD(I)
NEXT
