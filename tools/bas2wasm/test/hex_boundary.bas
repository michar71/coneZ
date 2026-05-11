' Hex literals at int32/int64 boundaries — both &H and 0x forms
' &H without & suffix: auto-promotes to i64 if > 2^31-1
A = &H7FFFFFFF             ' max i32
B& = &H80000000&           ' just over max i32 (needs & to disambiguate)
C& = &HFFFFFFFF&           ' all-ones 32-bit
D& = &H100000000&          ' first 33-bit value
E = 0x7FFFFFFF
F& = 0x80000000
G& = 0xFFFFFFFFFFFFFFFF&   ' all-ones 64-bit
FORMAT "% & & & % & &", A, B&, C&, D&, E, F&, G&
