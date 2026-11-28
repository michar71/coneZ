' HOSTS: real
' EXPECTED:
' 120
' TRAP: [trap] stack overflow

' Recursion deeper than the wasm3 value stack (8 KB on the device and in the
' simulator) stops the program with "stack overflow" and the BASIC line
' number, rather than crashing. Only a real host has this limit -- the Node
' harness runs on V8's much larger stack -- hence HOSTS: real.
FUNCTION COUNT_DOWN N
  IF N <= 0 THEN RETURN 0
  RETURN COUNT_DOWN(N - 1) + 1
END FUNCTION

> COUNT_DOWN(120)
> COUNT_DOWN(500)
> "not reached"
