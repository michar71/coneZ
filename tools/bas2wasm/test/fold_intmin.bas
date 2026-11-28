' EXPECTED:
' 0
' TRAP: divide result unrepresentable

' INT_MIN / -1 must not crash the compiler while folding constants. It is left
' to run time: MOD gives 0, division overflows and traps.
> (-2147483647 - 1) MOD -1
> (-2147483647 - 1) \ -1
