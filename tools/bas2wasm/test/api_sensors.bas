' Sensor API smoke test (IMU + env, float-returning forms)
P# = PITCH#
R# = ROLL#
Y# = YAW#
AX# = ACCX#
AY# = ACCY#
AZ# = ACCZ#
T# = TEMP#
H# = HUM#
B# = BRIGHT#
FORMAT "& & & & & & & & &", P#, R#, Y#, AX#, AY#, AZ#, T#, H#, B#

' i32-returning forms
PI = PITCH
ROL = ROLL
YA = YAW
TEMP_I = TEMP
FORMAT "% % % %", PI, ROL, YA, TEMP_I
