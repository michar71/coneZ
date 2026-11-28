' EXPECTED:
' 5
' 0
' 10
' 100
' 255
' 0

' LIMIT / LIMIT256 clamp (they used to return the minimum).
> LIMIT(5, 0, 10)
> LIMIT(-5, 0, 10)
> LIMIT(50, 0, 10)
> LIMIT256(100)
> LIMIT256(300)
> LIMIT256(-5)
