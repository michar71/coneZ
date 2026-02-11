' File I/O test program
' Write data to a file, then read it back

' Write various types
OPEN "/test_io.txt" FOR OUTPUT AS #1
PRINT #1, "Hello World"
PRINT #1, 42
PRINT #1, 3.14
CLOSE #1

' Read it back
OPEN "/test_io.txt" FOR INPUT AS #2
WHILE NOT EOF(2)
    INPUT #2, LINE$
    FORMAT "Read: $", LINE$
WEND
CLOSE #2

' Append mode
OPEN "/test_io.txt" FOR APPEND AS #3
PRINT #3, "Appended line"
CLOSE #3

' Read again to verify append
OPEN "/test_io.txt" FOR INPUT AS #1
COUNT = 0
WHILE NOT EOF(1)
    INPUT #1, L$
    COUNT = COUNT + 1
WEND
CLOSE #1
FORMAT "Total lines: %", COUNT

' File management
MKDIR "/testdir"
NAME "/test_io.txt" AS "/testdir/moved.txt"
KILL "/testdir/moved.txt"
RMDIR "/testdir"

FORMAT "Done"
