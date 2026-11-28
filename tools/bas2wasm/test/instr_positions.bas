' EXPECTED:
' 1
' 1
' 3
' 1
' 4
' 4
' 0
' 3
' 6
' 6
' 0
' 0
' 1
' 0
' 0
' 1
' 5
' 5
' 8
' 11
' 11

' INSTR positions are 1-based, and a match may begin at the very first
' character or exactly at the start position.
> INSTR("abcabc", "a")
> INSTR("abcabc", "abc")
> INSTR("abcabc", "c")
> INSTR("abcabc", "abc", 1)
> INSTR("abcabc", "abc", 2)
> INSTR("abcabc", "abc", 4)
> INSTR("abcabc", "abc", 5)
> INSTR("abcabc", "c", 3)
> INSTR("abcabc", "c", 4)
> INSTR("abcabc", "c", 6)
> INSTR("abcabc", "c", 7)
> INSTR("abcabc", "x")
> INSTR("abcabc", "abcabc")
> INSTR("abcabc", "abcabcd")
> INSTR("", "a")
A$ = "hello world"
> INSTR(A$, "h")
> INSTR(A$, "o")
> INSTR(A$, "o", 5)
> INSTR(A$, "o", 6)
> INSTR(A$, "d")
> INSTR(A$, "d", 11)
