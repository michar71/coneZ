' HOSTS: node fwhost   (the simulator has no pins: PIN_READ is always 0 there)
' PIN_HIGH / PIN_LOW are predicates (-1 / 0), so NOT and IF work on them
' directly. PIN_READ stays the pin level, 1 or 0, for comparisons and
' arithmetic. (In the harness odd-numbered pins read high, even read low.)

' EXPECTED:
' 1
' 0
' -1
' 0
' 0
' -1
' 0
' -1
' pin 3 high
' pin 4 low
' pin 4 not high
' pin 3 not low
' both
' 255
' level compare ok
' 2

> PIN_READ(3)
> PIN_READ(4)
> PIN_HIGH(3)
> PIN_HIGH(4)
> PIN_LOW(3)
> PIN_LOW(4)
> NOT PIN_HIGH(3)
> NOT PIN_LOW(3)
IF PIN_HIGH(3) THEN > "pin 3 high"
IF PIN_LOW(4) THEN > "pin 4 low"
IF NOT PIN_HIGH(4) THEN > "pin 4 not high"
IF NOT PIN_LOW(3) THEN > "pin 3 not low" ELSE > "wrong"
IF PIN_HIGH(3) AND NOT PIN_HIGH(4) THEN > "both"
' PIN_READ is still a number: arithmetic and explicit comparison
> PIN_READ(3) * 255
IF NOT PIN_READ(4) = 1 THEN > "level compare ok"
' the pin argument is an expression
P = 5
N = 0
FOR I = 0 TO 3
  IF PIN_HIGH(P + I) THEN N = N + 1
NEXT
> N
