' Should fail: nested SUB definitions are not supported
SUB OUTER
  SUB INNER
    FORMAT "inner"
  END SUB
END SUB

OUTER
