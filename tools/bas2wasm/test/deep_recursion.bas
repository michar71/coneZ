' EXPECTED:
' 100: 100
' 500: 500

' Deep recursion — exercises WASM stack-size flag in linker (well, the WASM
' interpreter's host stack via wasm3). Compiles to a simple tail-shaped recursion.
FUNCTION COUNT_DOWN N
  IF N <= 0 THEN RETURN 0
  RETURN COUNT_DOWN(N - 1) + 1
END FUNCTION

FORMAT "100: %", COUNT_DOWN(100)
FORMAT "500: %", COUNT_DOWN(500)
