' EXPECTED:
' ok

' SETLEDRGB(aR, aG, aB) — loops over LED count, reads each array, writes pixels
DIM AR(50)
DIM AG(50)
DIM AB(50)
FOR I = 1 TO 50
  AR(I) = I * 5
  AG(I) = 255 - I * 5
  AB(I) = 128
NEXT
S = SETLEDRGB(AR, AG, AB)
> "ok"
