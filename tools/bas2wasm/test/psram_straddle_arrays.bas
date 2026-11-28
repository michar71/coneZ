' EXPECTED:
' 0
' 0
' 0
' 0
' -3447
' 2800000245350
' after the arrays: 4000000700

' The same 4 KB line (see psram_straddle_strings.bas), crossed by the host's
' zero fill of array memory. This program has almost no data, so its first
' array starts near address 0 and lies across 4096.
DIM A(1500)
S = 0
FOR I = 1 TO 1500
  S = S + A(I)
NEXT
> S
FOR I = 1 TO 1500
  A(I) = I * 3 + 1
NEXT
' Free it and take the same space again: the host must hand it back zeroed,
' on both sides of the line (it is full of the values above).
ERASE A
DIM C(1400)
Z = 0
NZ = 0
FOR I = 1 TO 1400
  IF C(I) <> 0 THEN NZ = NZ + 1
  Z = Z + C(I)
NEXT
> Z
> NZ
' REDIM PRESERVE keeps every element when the array grows (and moves)
FOR I = 1 TO 1400
  C(I) = 0 - I
NEXT
REDIM PRESERVE C(2600)
BAD = 0
FOR I = 1 TO 1400
  IF C(I) <> 0 - I THEN BAD = BAD + 1
NEXT
FOR I = 1401 TO 2600
  IF C(I) <> 0 THEN BAD = BAD + 1
NEXT
> BAD
> C(1400) + C(1023) + C(1024)
' an i64 array and a string built from it still work alongside
DIM B&(700)
T& = 0&
FOR I = 1 TO 700
  B&(I) = 4000000000& + I
  T& = T& + B&(I)
NEXT
> T&
N$ = "after the arrays: " + STR$(B&(700))
> N$
