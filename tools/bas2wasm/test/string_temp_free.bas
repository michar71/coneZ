' EXPECTED:
' 300
' a300b
' 300
' two
' 300

' String temporaries are freed. The firmware's string pool has 128 blocks
' (the harness models that), so a per-iteration leak fails long before 300.
DIM N$(3)
N$(1) = "red"
N$(2) = "green"
N$(3) = "blue"
C = 0
G = 0
FOR I = 1 TO 300
  S$ = "a" + STR$(I) + "b"
  IF LEFT$(S$, 1) = "a" THEN C = C + 1
  IF N$(I MOD 3 + 1) + "!" <> "green?" THEN G = G + 1
  L = LEN(UPPER$(S$) + LOWER$(S$))
NEXT
> C
> S$
> G
SELECT CASE "t" + "wo"
CASE "one"
  > "one"
CASE "t" + "wo"
  > "two"
END SELECT
K = 0
FOR I = 1 TO 300
  SELECT CASE STR$(I MOD 2)
  CASE "0"
    K = K + 1
  CASE "1"
    K = K + 1
  END SELECT
NEXT
> K
