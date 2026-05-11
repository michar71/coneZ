' IF / ELSE / ELSEIF chains — multi-line form omits THEN
X = 5

IF X = 1
  FORMAT "one"
ELSE
  FORMAT "not one"
END IF

IF X = 1
  FORMAT "one"
ELSEIF X = 5
  FORMAT "five"
ELSEIF X = 10
  FORMAT "ten"
ELSE
  FORMAT "other"
END IF

' Single-line IF/THEN
IF X > 0 THEN FORMAT "positive"

' Nested IFs
IF X > 0
  IF X < 10
    FORMAT "small positive"
  END IF
END IF
