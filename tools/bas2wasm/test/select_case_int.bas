' SELECT CASE with integer expression, including IS, comma list, and ELSE
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
