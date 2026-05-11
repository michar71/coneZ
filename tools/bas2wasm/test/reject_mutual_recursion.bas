' Should fail: bas2wasm is single-pass with no forward declarations,
' so mutual recursion is rejected ("not a function" at the forward reference).
FUNCTION IS_EVEN N
  IF N = 0 THEN RETURN -1
  RETURN IS_ODD(N - 1)
END FUNCTION

FUNCTION IS_ODD N
  IF N = 0 THEN RETURN 0
  RETURN IS_EVEN(N - 1)
END FUNCTION

> IS_EVEN(4)
