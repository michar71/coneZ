' EXIT FOR / WHILE / DO / SELECT
FOR I = 1 TO 100
  IF I = 5 THEN EXIT FOR
NEXT
FORMAT "exit_for=%", I

K = 0
WHILE K < 100
  K = K + 1
  IF K = 3 THEN EXIT WHILE
WEND
FORMAT "exit_while=%", K

J = 0
DO
  J = J + 1
  IF J = 4 THEN EXIT DO
LOOP
FORMAT "exit_do=%", J

X = 2
SELECT CASE X
  CASE 1
    FORMAT "one"
  CASE 2
    FORMAT "two"
    EXIT SELECT
  CASE 3
    FORMAT "three"
END SELECT
