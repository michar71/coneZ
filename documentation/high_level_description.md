This is the high-levle descrition for the coneZ project.

General Goals
=============
The genral goal is to control different devices for either a predefined or life performance in a coordinated way.

Low Bandwidth:
- Deployment might happen in an environment with high utalisiation of existing bandwidth (Too many wireless devices) or over a large area (A whole desert)
LoRa is used to ditribute data over large distances to individual devices.

- High flexibility
All mind of different devices shall be supported. (Individual Light,s LED Strips, single point events like fire poofers, large art instsllations)

- Singluar time reference
Events are triggered by time. Devices can use GPS or NTP to manage time, genrally GPS shall provide the reference time.

- Extendable concepts

- Robust set of tools




High-Level Concepts
===================


This is the hirachy used for describing the elements used.

- Shitshow
At the highlest level is the shitshoew. (Often referenced as "The Show"). It consist of 

- Pieces
Which are indidual parts of the Show. (Media like audio or video or life perfomance). Technically each of these are a Piece of Shit. 
Each piece that is supposed to have effects has an accoicated

- Cue-List
The cue list defines point in time in relationship to the start of the piece whne thinga are happening. The cue-list is made up of individual

- Cues
Each cue defines a link to an effect, its parameters, a start point in time and a duration. (While cues usue exist in the concept of a cue list they can also be send as indivual pieces. If that is happening the timestamp needs to be in the future otherwise you will need a time machine to see the effect....)

- Effects
Effects are what is actually happening. Those can be simple "Set LED to color X" effects or "Start script rainbow.bas thsat is soumd-active with the following parameters".
Effects are device-depenandant but some effects (Like color) probably work on most devices.


Transport/Protocols
-------------------
The folowing transport layers hall be supported:
- Quantum Tunneling Transfer Protcol (QTTP)
- Ethernet (TCP/IP)
- Wifi (TCP/IP)
- Lora Data blocks (64 bytes)
- Carrier Pidgeon Transfer Protocol (CPTP)

Workflow
========
1) Generate a cue -list for a piece  (Mayhem)
2) Assoicated Effects with cues (Mayhem)
3) Develop/Simulate effects
4) Export Cuelist/Scripts as "Piece" package
5) Compibne multiple pieces into a show.
6) Deploy Data to devices

7) Control parameters and Peice selectors in real-time


Cue-Types
=========
- Piece Selector (Sets the start time for a pre-deployed cue-list at a specific time)
- Script Effect
- Color Effect
- Pre-defined FX
- Channel Parameter Parameter (Changes indfividual Parmeter for a channel)
- Global Control Parameter (Overall Brightness/Volume Control)

Data Structures
===============

Actual Implementation
=====================
