' LOCAL inside SUB shadows the same-named global
G = 100
SUB INNER
  LOCAL G
  G = 7
  FORMAT "inner: %", G
END SUB

INNER
FORMAT "outer: %", G
