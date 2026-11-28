' EXPECTED:
' 50: 50
' 100: 100
' 5050

' Deep recursion. Each nested call takes space on the wasm3 interpreter's
' 8 KB value stack; a small function like these manages about 125 levels on
' the device (see recursion_limit.bas for what happens beyond that).
FUNCTION COUNT_DOWN N
  IF N <= 0 THEN RETURN 0
  RETURN COUNT_DOWN(N - 1) + 1
END FUNCTION

FUNCTION SUMTO N
  IF N <= 0 THEN RETURN 0
  RETURN N + SUMTO(N - 1)
END FUNCTION

FORMAT "50: %", COUNT_DOWN(50)
FORMAT "100: %", COUNT_DOWN(100)
> SUMTO(100)
