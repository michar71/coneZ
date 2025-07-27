#Subroutine to strobe LEDs and return them to low red glow
SUB STROBE R,G,B
    II = 255
    #set all LEDs to white
    FOR I = 0 TO 255
        S = SETLEDCOL(255-I,255-I,255-I)
        S = WAIT(3)
    END FOR
    #set all LEDs to preselected color
    S = SETLEDCOL(R,G,B)
END SUB

#check if we have GPS signal
FORMAT "WAITING FOR GPS"
STARTTIME = TIMESTAMP(1000)
ENDTIME = STARTTIME
GPS = 0
WHILE (GPS = 0)
    GPS = HASGPS() 
    S = WAIT(1000)
	ENDTIME = TIMESTAMP(1000)
    IF (ENDTIME-STARTTIME) > 300)
        FORMAT "GPS TIMEOUT"
        BYE
    END IF
END WHILE
FORMAT "GPS OK"

#check if we have an origin location
FORMAT "WAITING FOR ORIGIN"
STARTTIME = TIMESTAMP(1000)
ENDTIME = STARTTIME
ORG = 0
WHILE (ORG = 0)
    ORG = HASORIGIN() 
    S = WAIT(1000)
	ENDTIME = TIMESTAMP(1000)
    IF (ENDTIME-STARTTIME) > 300)
        FORMAT "ORG TIMEOUT"
        BYE
    END IF
END WHILE
FORMAT "ORG OK"

#Strobe one green
STROBE 0,32,0

LASTSEC = 0
SEC = 0
DIST = 0
TDMS = 0

#exit on stop signal
WHILE (GETPARAM(0) = 0)
    #Gegt time in Seconds
    SEC = SECOND()
    #wait for second rollover for every 4th second
    IF (SEC <> LASTSEC) AND (SEC \ 4 = 0)
        #delay by offset based on distance
        S = WAIT(TDMS)
        STROBE 8,0,0
        FORMAT "PING"
        LASTSEC = SEC

        #calculate distance to origin
        #We do this here in the loop because we have 4 seconds available....
        DIST = ORIGINDIST()
        TDMS = (DIST * 1000) /343
    END IF
END WHILE
FORMAT "STOPPED"
BYE