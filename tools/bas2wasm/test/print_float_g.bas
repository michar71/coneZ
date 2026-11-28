' `>` and STR$ print a float as C "%g" on the device and the simulator:
' six significant digits, no trailing zeros, exponent form for very large or
' very small values. FORMAT "&" is the way to get fixed decimals.

' EXPECTED:
' 1.5
' 40.7622
' 100000
' 1e+06
' 1.23457e+06
' 1.234e-05
' 0.666667
' -2.25
' 0
' 40.7622
' 40.762173

A# = 1.5
> A#
LAT# = 40.762173
> LAT#
B# = 100000.0
> B#
B# = B# * 10.0
> B#
C# = 1234567.0
> C#
D# = 0.00001234
> D#
E# = 2.0
> E# / 3.0
> 0.0 - 2.25
> A# - A#
> STR$(LAT#)
FORMAT "&", LAT#
