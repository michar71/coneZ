' Should fail: too few arguments to a known function
FUNCTION ADD3 A, B, C
  RETURN A + B + C
END FUNCTION

X = ADD3(1, 2)
> X
