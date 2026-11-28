' EXPECTED:
' 5
' 100
' ok

' DIM / REDIM that run more than once free the previous array (the low heap
' has 32 blocks), and a string array can be DIMmed again after ERASE.
SUB WORK
  DIM TMP(10)
  TMP(1) = 5
END SUB
FOR I = 1 TO 200
  WORK()
NEXT
> TMP(1)
FOR I = 1 TO 100
  REDIM BUF(50)
  BUF(1) = I
NEXT
> BUF(1)
DIM A$(2)
A$(1) = "x" + "y"
ERASE A$
DIM A$(4)
A$(1) = "ok"
> A$(1)
