' EXPECTED:
' side
' 42 r
' inner 2
' 1.500000 7 9.500000

' An argument that itself prints must not disturb the outer FORMAT's other
' arguments (all prints share one buffer).
FUNCTION SHOUT$ A
  > "side"
  RETURN "r"
END FUNCTION
FUNCTION FOO# X#
  FORMAT "inner %", 2
  RETURN X# + 7.5
END FUNCTION
FORMAT "% $", 42, SHOUT$(1)
FORMAT "& % &", 1.5, 7, FOO#(2.0)
