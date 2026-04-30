# Networking Two Picos

__This is for my Networking (ECE435) and Communications (ECE484) projects. The foci (plural?) of the two project reports are different enough and the development effort was sufficient enough that I feel this overlap is justifiable. This is meant, also, to integrate with my thesis project which is a piece of scientific instrumentation which needs to stream data and at present does so via USB.__

There are two picos.
One is on the ADPC. The other is on the computer.

One pico should be a WiFi access point (WAP). This should be the ADPC Pico.
This will be a **PICO 2 W** because I have one.

The other should **connect** to the ADPC Pico. This is the Computer Pico. 
This will be a **PICO W** because I have one.

## Connection

Upon connection to the WAP, the Computer Pico should be assigned an IP address. 
The DHCP server (from WAP example) handles this. 

## Data Streaming

Now that we have the **ability** to stream, let's do some actual **application**!!

UDP messages have a (potentially arbitrary) maximum length of 127 bytes. This is 63, 16-bit samples. So for our casptone_adc buffer size of 256 samples, we need **four** UDP sends.


