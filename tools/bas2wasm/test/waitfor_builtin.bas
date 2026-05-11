' WAITFOR(event, source, condition, trigger, timeout) — 5-arg builtin
' that dispatches to delay_ms / wait_pps / wait_param based on event.
' All if-blocks must have void signature; result flows through a scratch local.

' Time-based wait (event=4). cond=6 → trig * 3600000 ms (hours)
R1 = WAITFOR(4, 0, 6, 1, 0)

' Time-based wait. cond=7 → trig * 60000 ms (minutes)
R2 = WAITFOR(4, 0, 7, 1, 0)

' Time-based wait. cond=8 → trig * 1000 ms (seconds)
R3 = WAITFOR(4, 0, 8, 1, 0)

' PPS wait (event=5)
R4 = WAITFOR(5, 0, 0, 0, 100)

' Param wait (event=6)
R5 = WAITFOR(6, 1, 1, 50, 200)

' Default branch (unknown event) returns 0
R6 = WAITFOR(99, 0, 0, 0, 0)

FORMAT "% % % % % %", R1, R2, R3, R4, R5, R6

' Inside a larger expression — confirms the result lands on the stack correctly
X = WAITFOR(99, 0, 0, 0, 0) + 7
FORMAT "x=%", X
