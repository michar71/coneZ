' TIMESTAMP / TIMESTAMP& — uptime / divisor
T = TIMESTAMP(1000)        ' i32 millis / 1000 = seconds
S = T + 1
FORMAT "ts=% s=%", T, S

T2& = TIMESTAMP&(1000&)    ' i64 millis / 1000 = seconds
S2& = T2& + 1&
FORMAT "ts64=& s64=&", T2&, S2&

' Use inside a larger expression — exposes vstack-leak bugs if present
X = TIMESTAMP(100) * 2 + 1
FORMAT "x=%", X
