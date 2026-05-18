' SELECT CASE with integer expression, including IS, comma list, and ELSE

' EXPECTED:
' big
' 1: one
' 2: two-three
' 3: two-three
' 4: middle
' 5: middle
' 6: ge6
' 7: ge6
' 8: ge6
X = 7
SELECT CASE X
  CASE 1
    FORMAT "one"
  CASE 2, 3, 4
    FORMAT "small"
  CASE IS > 5
    FORMAT "big"
  CASE ELSE
    FORMAT "other"
END SELECT

' Each branch reached at least once across multiple inputs
FOR I = 1 TO 8
  SELECT CASE I
    CASE 1
      FORMAT "%: one", I
    CASE 2, 3
      FORMAT "%: two-three", I
    CASE IS >= 6
      FORMAT "%: ge6", I
    CASE ELSE
      FORMAT "%: middle", I
  END SELECT
NEXT
