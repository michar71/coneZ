' Should fail: too many arguments
FUNCTION ID X
  RETURN X
END FUNCTION

Y = ID(1, 2, 3)
> Y
