' Float arrays — DIM A#(...) with f32 cells
DIM F#(5)
F#(1) = 1.5
F#(2) = 2.25
F#(3) = -3.14
F#(4) = 1e3
F#(5) = 0.0
FORMAT "& & & & &", F#(1), F#(2), F#(3), F#(4), F#(5)

' Multi-dim float
DIM G#(2, 3)
G#(1, 1) = 1.1
G#(2, 3) = 2.3
FORMAT "& &", G#(1, 1), G#(2, 3)

' Loop-driven fill
DIM H#(10)
FOR I = 1 TO 10
  H#(I) = I * 0.5
NEXT
SUM# = 0.0
FOR J = 1 TO 10
  SUM# = SUM# + H#(J)
NEXT
FORMAT "sum=&", SUM#

' REDIM PRESERVE on a float array
DIM P#(3)
P#(1) = 10.5
P#(2) = 20.5
REDIM PRESERVE P#(6)
FORMAT "& &", P#(1), P#(2)
