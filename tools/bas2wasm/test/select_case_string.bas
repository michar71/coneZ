' EXPECTED:
' hello!

' SELECT CASE with string expression
S$ = "hello"
SELECT CASE S$
  CASE "world"
    FORMAT "world"
  CASE "hello"
    FORMAT "hello!"
  CASE ELSE
    FORMAT "unknown"
END SELECT
