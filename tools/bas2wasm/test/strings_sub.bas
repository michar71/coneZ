' Test string params in SUB
SUB GREET NAME$
  FORMAT "Hello $", NAME$
END SUB

GREET "World"
GREET("Alice")

' Test string FUNCTION
FUNCTION EXCLAIM$ MSG$
  RETURN MSG$ + "!"
END FUNCTION

A$ = EXCLAIM$("Wow")
FORMAT "Result: $", A$

' Test string LOCAL inside SUB
SUB PROCESS X$
  LOCAL TMP$
  TMP$ = X$ + " processed"
  FORMAT "Inside: $", TMP$
END SUB

PROCESS "data"

' Test that caller's string is not corrupted
B$ = "original"
SUB MODIFY S$
  S$ = "modified"
END SUB
MODIFY B$
FORMAT "After: $", B$

' Test string param with multiple params (mixed types)
SUB MIXED N, S$, V#
  FORMAT "N=% S=$ V=&", N, S$, V#
END SUB

MIXED 42, "hello", 3.14
