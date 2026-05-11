' i64 arrays (DIM A&) — element-size 8 path
DIM L&(5)
L&(1) = 9999999999&
L&(2) = -1&
L&(3) = 1234567890123&
FORMAT "& & &", L&(1), L&(2), L&(3)

' Multi-dim i64
DIM M&(2, 3)
M&(1, 1) = 100&
M&(2, 3) = 200&
FORMAT "& &", M&(1, 1), M&(2, 3)

' REDIM PRESERVE — must zero-init new elements with i64.const 0
DIM P&(3)
P&(1) = 42&
P&(2) = 99&
REDIM PRESERVE P&(8)
FORMAT "& & &", P&(1), P&(2), P&(5)
