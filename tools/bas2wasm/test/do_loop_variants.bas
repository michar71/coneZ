' All five DO/LOOP variants

' Infinite + EXIT DO
I = 0
DO
  I = I + 1
  IF I >= 3 THEN EXIT DO
LOOP
FORMAT "infinite: %", I

' DO WHILE (pre-condition)
J = 0
DO WHILE J < 5
  J = J + 1
LOOP
FORMAT "while: %", J

' DO UNTIL (pre-condition)
K = 0
DO UNTIL K = 5
  K = K + 1
LOOP
FORMAT "until: %", K

' LOOP WHILE (post-condition)
L = 0
DO
  L = L + 1
LOOP WHILE L < 3
FORMAT "loopwhile: %", L

' LOOP UNTIL (post-condition)
M = 0
DO
  M = M + 1
LOOP UNTIL M = 3
FORMAT "loopuntil: %", M
