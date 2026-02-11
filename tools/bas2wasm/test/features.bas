' Test QuickBASIC-compatible features

' Integer division and MOD
A = 17 \ 3
B = 17 MOD 3
FORMAT "17 \\ 3 = %, 17 MOD 3 = %", A, B

' NEXT
FOR I = 1 TO 3
  FORMAT "I=%", I
NEXT

' WEND
J = 3
WHILE J > 0
  FORMAT "J=%", J
  J = J - 1
WEND

' FUNCTION
FUNCTION DOUBLE X
  RETURN X * 2
END FUNCTION
FORMAT "DOUBLE(5)=%", DOUBLE(5)

' # suffix for float
PI# = 3.14159
FORMAT "PI=&", PI#

' String aliases
A$ = "  hello  "
FORMAT "$", LTRIM$(A$)
FORMAT "$", RTRIM$(A$)
FORMAT "$", UCASE$("hello")
FORMAT "$", LCASE$("HELLO")
