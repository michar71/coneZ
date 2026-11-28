' EXPECTED:
' two
' other

FOR I = 2 TO 3
  SELECT CASE I
  CASE 1
    > "one"
  CASE 2
    > "two"
  CASE ELSE
    > "other"
  END SELECT
NEXT
