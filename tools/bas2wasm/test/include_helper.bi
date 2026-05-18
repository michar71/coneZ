' Shared helpers pulled in via $INCLUDE (QuickBASIC .bi convention).
' Not a standalone test — the .bas runner only picks up *.bas files.
CONST PI2 = 6.28318
FUNCTION TRIPLE N
  RETURN N * 3
END FUNCTION
FUNCTION CLAMP255 X
  IF X < 0 THEN RETURN 0
  IF X > 255 THEN RETURN 255
  RETURN X
END FUNCTION
