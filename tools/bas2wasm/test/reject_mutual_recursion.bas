' Should fail: bas2wasm is single-pass, so mutual recursion needs an
' explicit DECLARE FUNCTION before the first reference. Without DECLARE,
' the forward call IS_ODD inside IS_EVEN is rejected ("not a function").
' See mutual_recursion.bas for the working pattern.
FUNCTION IS_EVEN N
  IF N = 0 THEN RETURN -1
  RETURN IS_ODD(N - 1)
END FUNCTION

FUNCTION IS_ODD N
  IF N = 0 THEN RETURN 0
  RETURN IS_EVEN(N - 1)
END FUNCTION

> IS_EVEN(4)
