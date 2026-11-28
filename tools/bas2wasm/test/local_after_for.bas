' EXPECTED:
' 7
' 42

' LOCAL restores the right value even when other code in the SUB allocated
' WASM locals before it.
G = 7
H = 42
SUB FOO
  FOR K = 1 TO 2
  NEXT
  LOCAL G
  G = 99
  DIM Q(3)
  Q(1) = 1
  LOCAL H
  H = 1
END SUB
FOO()
> G
> H
