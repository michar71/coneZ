' LOCAL declarations with non-string types (i32, f32, i64)
SUB DEMO
  LOCAL X
  LOCAL F#
  LOCAL L&
  X = 42
  F# = 3.14
  L& = 9999999999&
  FORMAT "i=% f=& l=&", X, F#, L&
END SUB

DEMO
