' EXPECTED:
' 3
' 77
' 5

' A definition may name its parameters differently from its DECLARE; the
' definition's names are the ones saved and restored.
DECLARE SUB SHOW N
A = 5
V = 77
SHOW(3)
> V
> A
SUB SHOW V
  > V
END SUB
