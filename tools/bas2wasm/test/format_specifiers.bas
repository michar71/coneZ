' EXPECTED:
' i=42 L=9999999999 f=3.14159 s=ConeZ
' 1 2 3 4
' 1.5 2.5 3.5
' alpha beta
' path: \foo\bar = 42
' hello, world
' sum=43 prod=84
' func=CONEZ

' FORMAT specifiers: % (i32/i64), & (f32), $ (string). Exercise all forms.
I = 42
L& = 9999999999&
F# = 3.14159
S$ = "ConeZ"

' One of each, in one call
FORMAT "i=% L=& f=& s=$", I, L&, F#, S$

' Multiple of the same specifier
FORMAT "% % % %", 1, 2, 3, 4
FORMAT "& & &", 1.5, 2.5, 3.5
FORMAT "$ $", "alpha", "beta"

' Escape sequences in format string: \\ for literal backslash
FORMAT "path: \\foo\\bar = %", I

' FORMAT with no args (literal string)
FORMAT "hello, world"

' FORMAT with expressions as args (not just variables)
FORMAT "sum=% prod=%", I + 1, I * 2
FORMAT "func=$", UPPER$(S$)
