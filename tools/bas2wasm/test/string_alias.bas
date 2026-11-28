' EXPECTED:
' abcdef
' xy
' item1
' abcdef
' zzzzz

' A string variable owns its value. Assigning from another variable copies,
' so reassigning (and freeing) one does not disturb the other; a FUNCTION
' returning a LOCAL's string returns a copy. The harness poisons freed blocks,
' so any use-after-free shows up as '#' characters.
A$ = "abc" + "def"
B$ = A$
A$ = "x" + "y"
C$ = "q" + "r"
> B$
A$ = A$
> A$
FUNCTION TAG$ N
  LOCAL T$
  T$ = "item" + STR$(N)
  RETURN T$
END FUNCTION
R$ = TAG$(1)
D$ = "x" + "yz12345"
> R$
DIM N$(2)
N$(1) = B$
B$ = "zz" + "zzz"
> N$(1)
> B$
