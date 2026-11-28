' EXPECTED:
' !!!
' !!!
' v=3
' v=4
' 77

' A SUB can be called bare, with arguments, or with parentheses.
SUB SHOUT
  FORMAT "!!!"
END SUB
SUB SHOW V
  FORMAT "v=%", V
END SUB
FUNCTION NAME$ N
  RETURN "n" + STR$(N)
END FUNCTION
SHOUT
SHOUT()
SHOW 3
SHOW(4)
NAME$(5)
> 77
