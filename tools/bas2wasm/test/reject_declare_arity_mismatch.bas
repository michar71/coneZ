' Should fail: definition has fewer params than DECLARE
DECLARE FUNCTION F A, B, C

FUNCTION F A, B
  RETURN A + B
END FUNCTION

> F(1, 2, 3)
