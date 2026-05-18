' QuickBASIC-style $INCLUDE: pull functions/consts from a .bi file.
' Both the '$INCLUDE: and REM $INCLUDE: spellings are accepted; here we
' use the apostrophe form. The file is spliced in at this point.

' EXPECTED:
' 21
' 30
' 0
' 255
' 100
' 6
'$INCLUDE: 'include_helper.bi'

> TRIPLE(7)
> TRIPLE(10)
> CLAMP255(-5)
> CLAMP255(999)
> CLAMP255(100)
R = PI2 \ 1
> R
