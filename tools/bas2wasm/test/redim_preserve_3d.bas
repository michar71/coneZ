' REDIM PRESERVE on a 3D array — stride changes through multiple levels

' EXPECTED:
' 111
' 223
' 122
DIM D(2, 2, 3)
D(1, 1, 1) = 111
D(2, 2, 3) = 223
D(1, 2, 2) = 122
REDIM PRESERVE D(2, 4, 3)
' Expected: 111, 223, 122 — every value remapped to its new (i,j,k) cell
> D(1,1,1)
> D(2,2,3)
> D(1,2,2)
