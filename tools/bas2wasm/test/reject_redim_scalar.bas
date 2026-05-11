' Should fail: REDIM PRESERVE on a variable that was never DIM'd as an array
X = 5
REDIM PRESERVE X(10)
> X
