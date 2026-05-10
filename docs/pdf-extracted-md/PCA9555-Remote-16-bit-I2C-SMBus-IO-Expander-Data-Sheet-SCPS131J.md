# PCA9555 Remote 16-bit I2C and SMBus I/O Expander with Interrupt Output and Configuration Registers datasheet (Rev. J)

- Source PDF: `docs/PCA9555-Remote-16-bit-I2C-SMBus-IO-Expander-Data-Sheet-SCPS131J.pdf`
- Extraction date: 2026-05-09
- Page count: 49
- SHA256: `4f7889fb66ee30063279b385658820d9e4fda7b05342e6c34bcf9c613bebc290`

## Page 1

PCA9555 Remote 16-bit I2C and SMBus I/O Expander with Interrupt Output and
Configuration Registers
1 Features
- Low Standby-Current Consumption of 1 uA Max
- I 2C to Parallel Port Expander
- Open-Drain Active-Low Interrupt Output
- 5-V Tolerant I/O Ports
- Compatible With Most Microcontrollers
- 400-kHz Fast I 2C Bus
- Address by Three Hardware Address Pins for Use
of up to Eight Devices
- Polarity Inversion Register
- Latched Outputs With High-Current Drive
Capability for Directly Driving LEDs
- Latch-Up Performance Exceeds 100 mA Per
JESD 78, Class II
- ESD Protection Exceeds JESD 22
- 2000-V Human-Body Model (A114-A)
- 200-V Machine Model (A115-A)
- 1000-V Charged-Device Model (C101)
2 Applications
- Servers
- Routers (Telecom Switching Equipment)
- Personal Computers
- Personal Electronics
- Industrial Automation Equipment
- Products with GPIO-Limited Processors
3 Description
This 16-bit I/O expander for the two-line bidirectional
bus (I2C) is designed for 2.3-V to 5.5-V VCC operation.
It provides general-purpose remote I/O expansion for
most microcontroller families via the I 2C interface
[serial clock (SCL), serial data (SDA)].
The PCA9555 consists of two 8-bit Configuration
(input or output selection), Input Port, Output Port,
and Polarity Inversion (active high or active low
operation) registers. At power on, the I/Os are
configured as inputs. The system master can enable
the I/Os as either inputs or outputs by writing to the
I/O configuration bits. The data for each input or
output is kept in the corresponding Input or Output
register. The polarity of the Input Port register can
be inverted with the Polarity Inversion register. All
registers can be read by the system master.
Device Information (1)
PART NUMBER PACKAGE BODY SIZE (NOM)
PCA9555
SSOP (24) DB 8.20 mm x 5.30 mm
SSOP (24) DBQ 8.65 mm x 3.90 mm
TVSOP (24) DGV 5.00 mm x 4.40 mm
SOIC (24) DW 15.4 mm x 7.50 mm
SSOP (24) PW 7.80 mm x 4.40 mm
VQFN (24) RGE 4.00 mm x 4.00 mm
(1) For all available packages, see the orderable addendum at
the end of the datasheet.
22
I/O
Port
P17-P10
Shift
Register 16 Bits
LP FilterInterrupt
Logic
Input
Filter
23
Power-On
Reset
Read Pulse
Write Pulse
PCA9555
3
2
21
1
24
12GND
VCC
SDA
SCL
A2
A1
A0
INT
I2C Bus
Control
P07-P00
Block Diagram
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 1
Product Folder Links: PCA9555
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
An IMPORTANT NOTICE at the end of this data sheet addresses availability, warranty, changes, use in safety-critical applications,
intellectual property matters and other important disclaimers. PRODUCTION DATA.

## Page 2

Table of Contents
1 Features............................................................................1
2 Applications.....................................................................1
3 Description.......................................................................1
4 Revision History.............................................................. 2
5 Pin Configuration and Functions...................................4
6 Specifications.................................................................. 5
6.1 Absolute Maximum Ratings........................................ 5
6.2 ESD Ratings............................................................... 5
6.3 Recommended Operating Conditions.........................5
6.4 Thermal Information....................................................6
6.5 Electrical Characteristics.............................................7
6.6 I2C Interface Timing Requirements.............................7
6.7 Switching Characteristics............................................8
6.8 Typical Characteristics................................................9
7 Parameter Measurement Information..........................12
8 Detailed Description......................................................14
8.1 Overview...................................................................14
8.2 Functional Block Diagram.........................................14
8.3 Device Features........................................................15
8.4 Device Functional Modes..........................................16
8.5 Programming............................................................ 17
9 Application Information Disclaimer.............................24
9.1 Application Information............................................. 24
10 Power Supply Recommendations..............................27
10.1 Power-On Reset Requirements..............................27
11 Layout...........................................................................29
11.1 Layout Guidelines................................................... 29
11.2 Layout Example...................................................... 29
12 Device and Documentation Support..........................30
12.1 Receiving Notification of Documentation Updates..30
12.2 Support Resources................................................. 30
12.3 Trademarks.............................................................30
12.4 Electrostatic Discharge Caution..............................30
12.5 Glossary..................................................................30
13 Mechanical, Packaging, and Orderable
Information.................................................................... 30
4 Revision History
Changes from Revision I (April 2019) to Revision J (March 2021) Page
- Changed the V IH High-level input voltage (SDL, SDA) Max value From: 5.5 V To: VCC in the Recommended
Operating Conditions .........................................................................................................................................5
- Changed the values for the DB, PW, and RGE packages in the Thermal Information table..............................6
- Changed the V PORR row in the Electrical Characteristics ..................................................................................7
- Added the V PORF row in the Electrical Characteristics .......................................................................................7
- Changed the I CC Standby mode (High Inputs) values in the Electrical Characteristics .....................................7
- Changed the C i SCL Max value From: 7 pF To: 8 pF in the Electrical Characteristics ......................................7
- Changed the C io SDA Max value From: 7 pF To: 9.5 pF in the Electrical Characteristics .................................7
- Changed the Typical characteristic graphs.........................................................................................................9
- Changed the Power Supply Recommendations ..............................................................................................27
Changes from Revision H (April 2019) to Revision I (April 2019) Page
- Changed the I2C Interface Timing Requirements table...................................................................................... 7
Changes from Revision G (March 2018) to Revision H (April 2019) Page
- Changed the Device Information table............................................................................................................... 1
- Added the DW package to the Thermal Information table..................................................................................6
Changes from Revision F (June 2014) to Revision G (March 2018) Page
- Added the Applications list .................................................................................................................................1
- Removed the Thermal Information from the Absolute Maximum Ratings ......................................................... 5
- Added Storage temperature range to the Absolute Maximum Ratings ............................................................. 5
- Changed the Handling Ratings table to the ESD Ratings table..........................................................................5
- Added the Thermal Information table ................................................................................................................ 6
- Added the Design Requirements section ........................................................................................................ 25
- Added the Application Curves section .............................................................................................................26
- Added the Layout section ................................................................................................................................ 29
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
2 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 3

Changes from Revision E (May 2008) to Revision F (June 2014) Page
- Added Interrupt Errata section.......................................................................................................................... 16
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 3
Product Folder Links: PCA9555

## Page 4

5 Pin Configuration and Functions
1INT 24  VCC
2A1 23  SDA
3A2 22  SCL
4P00 21  A0
5P01 20  P17
6P02 19  P16
7P03 18  P15
8P04 17  P14
9P05 16  P13
10P06 15  P12
11P07 14  P11
12GND 13  P10
Not to scale
Figure 5-1. DB, DBQ, DGV, DW or PW Package, 24
Pin (SOP), (Top View)
24 A27P06
1P00 18  A0
23 A18P07
2P01 17  P17
22 INT9GND
3P02 16  P16
21 VCC 10P10
4P03 15  P15
20 SDA11P11
5P04 14  P14
19 SCL12P12
6P05 13  P13
Not to scale
Thermal
Pad
Figure 5-2. RGE Package, 24 Pin (QFN), (Top View)
Table 5-1. Pin Functions
PIN
DESCRIPTION
NAME
SSOP (DB),
QSOP (DBQ),
TSSOP (PW), AND
TVSOP (DGV)
QFN (RGE)
INT 1 22 Interrupt output. Connect to VCC through a pullup resistor.
A1 2 23 Address input 1. Connect directly to VCC or ground.
A2 3 24 Address input 2. Connect directly to VCC or ground.
P00 4 1 P-port input/output. Push-pull design structure.
P01 5 2 P-port input/output. Push-pull design structure.
P02 6 3 P-port input/output. Push-pull design structure.
P03 7 4 P-port input/output. Push-pull design structure.
P04 8 5 P-port input/output. Push-pull design structure.
P05 9 6 P-port input/output. Push-pull design structure.
P06 10 7 P-port input/output. Push-pull design structure.
P07 11 8 P-port input/output. Push-pull design structure.
GND 12 9 Ground
P10 13 10 P-port input/output. Push-pull design structure.
P11 14 11 P-port input/output. Push-pull design structure.
P12 15 12 P-port input/output. Push-pull design structure.
P13 16 13 P-port input/output. Push-pull design structure.
P14 17 14 P-port input/output. Push-pull design structure.
P15 18 15 P-port input/output. Push-pull design structure.
P16 19 16 P-port input/output. Push-pull design structure.
P17 20 17 P-port input/output. Push-pull design structure.
A0 21 18 Address input 0. Connect directly to VCC or ground.
SCL 22 19 Serial clock bus. Connect to VCC through a pullup resistor.
SDA 23 20 Serial data bus. Connect to VCC through a pullup resistor.
VCC 24 21 Supply voltage
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
4 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 5

6 Specifications
6.1 Absolute Maximum Ratings
over operating free-air temperature range (unless otherwise noted) (1)
MIN MAX UNIT
VCC Supply voltage range -0.5 6 V
VI Input voltage range(2) -0.5 6 V
VO Output voltage range(2) -0.5 6 V
IIK Input clamp current VI < 0 -20 mA
IOK Output clamp current VO < 0 -20 mA
IIOK Input/output clamp current VO < 0 or VO > VCC +/-20 mA
IOL Continuous output low current VO = 0 to VCC 50 mA
IOH Continuous output high current VO = 0 to VCC -50 mA
ICC
Continuous current through GND -250
mA
Continuous current through VCC 160
Tstg Storage temperature range -65 150  deg C
(1) Stresses beyond those listed under Absolute Maximum Ratings may cause permanent damage to the device. These are stress ratings
only, and functional operation of the device at these or any other conditions beyond those indicated under Recommended Operating
Conditions is not implied. Exposure to absolute-maximum-rated conditions for extended periods may affect device reliability.
(2) The input negative-voltage and output voltage ratings may be exceeded if the input and output current ratings are observed.
6.2 ESD Ratings
MIN MAX UNIT
V(ESD) Electrostatic discharge
Human body model (HBM), per ANSI/ESDA/JEDEC JS-001, all
pins(1) 0 2000
V
Charged device model (CDM), per JEDEC specification
JESD22-C101 or ANSI/ESDA/JEDEC JS-002, all pins(2) 0 1000
(1) JEDEC document JEP155 states that 500-V HBM allows safe manufacturing with a standard ESD control process.
(2) JEDEC document JEP157 states that 250-V CDM allows safe manufacturing with a standard ESD control process.
6.3 Recommended Operating Conditions
MIN MAX UNIT
VCC Supply voltage 2.3 5.5 V
VIH High-level input voltage
SCL, SDA 0.7 x VCC VCC (1)
V
A2-A0, P07-P00, P17-P10 0.7 x VCC 5.5
VIL Low-level input voltage
SCL, SDA -0.5 0.3 x VCC
V
A2-A0, P07-P00, P17-P10 -0.5 0.3 x VCC
IOH High-level output current P07-P00, P17-P10 -10 mA
IOL Low-level output current P07-P00, P17-P10 25 mA
TA Operating free-air temperature -40 85  deg C
(1) For voltages applied above V CC, an increase in ICC will result.
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 5
Product Folder Links: PCA9555

## Page 6

6.4 Thermal Information
THERMAL METRIC(1)
PCA9555
UNITDB
(SSOP)
DBQ
(QSOP)
DGV
(TVSOP)
DW
(SOIC)
PW
(TSSOP)
RGE
(QFN)
24 PINS 24 PINS 24 PINS 24 PINS 24 PINS 24 PINS
RJA Junction-to-ambient thermal resistance 92.9 81.8 105.4 66.7 108.8 48.4  deg C/W
R
JC(top)
Junction-to-case (top) thermal resistance 53.5 39.3 36.7 36.7 54 58.1  deg C/W
RJB Junction-to-board thermal resistance 50.4 36.0 50.8 36.7 62.8 27.1  deg C/W
JT Junction-to-top characterization parameter 21.9 7.6 2.4 13.1 11.1 3.3  deg C/W
JB Junction-to-board characterization parameter 50.1 35.6 50.3 62.3 62.3 27.2  deg C/W
R
JC(bot)
Junction-to-case (bottom) thermal resistance n/a n/a n/a n/a n/a 15.3  deg C/W
(1) For more information about traditional and new thermal metrics, see the Semiconductor and IC Package Thermal Metrics application
report.
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
6 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 7

6.5 Electrical Characteristics
over recommended operating free-air temperature range (unless otherwise noted)
PARAMETER TEST CONDITIONS VCC MIN TYP(1) MAX UNIT
VIK Input diode clamp voltage II = -18 mA 2.3 V to 5.5 V -1.2 V
VPORR Power-on reset voltage, VCC rising VI = VCC or GND, IO = 0 1.2 1.5 V
VPORF
Power-on reset voltage, VCC
falling VI = VCC or GND, IO = 0 0.75 1 V
VOH P-port high-level output voltage(2)
IOH = -8 mA
2.3 V 1.8
V
3 V 2.6
4.75 V 4.1
IOH = -10 mA
2.3 V 1.7
3 V 2.5
4.75 V 4
IOL
SDA VOL = 0.4 V
2.3 V to 5.5 V
3
mAP port(3) VOL = 0.5 V 8 20
VOL = 0.7 V 10 24
INT VOL = 0.4 V 3
II
SCL, SDA
VI = VCC or GND 2.3 V to 5.5 V
+/-1
uA
A2-A0 +/-1
IIH P port VI = VCC 2.3 V to 5.5 V 1 uA
IIL P port VI = GND 2.3 V to 5.5 V -100 uA
ICC
Operating mode VI = VCC or GND, IO = 0,
I/O = inputs, fSCL = 400 kHz, No load
5.5 V 100 200
uA3.6 V 30 75
2.7 V 20 50
Standby mode
Low inputs VI = GND, IO = 0, I/O = inputs,
fSCL = 0 kHz, No load
5.5 V 1.1 1.5
mA3.6 V 0.7 1.3
2.7 V 0.5 1
High inputs VI = VCC, IO = 0, I/O = inputs,
fSCL = 0 kHz, No load
5.5 V 2.5 3.5
uA3.6 V 1 1.8
2.7 V 0.7 1.6
ICC Additional current in standby mode One input at VCC - 0.6 V,
Other inputs at VCC or GND 2.3 V to 5.5 V 1.5 mA
CI SCL VI = VCC or GND 2.3 V to 5.5 V 3 8 pF
Cio
SDA
VIO = VCC or GND 2.3 V to 5.5 V
3 9.5
pF
P port 3.7 9.5
(1) All typical values are at nominal supply voltage (2.5-V, 3.3-V, or 5-V V CC) and TA = 25 deg C.
(2) Each I/O must be externally limited to a maximum of 25 mA, and each octal (P07-P00 and P17-P10) must be limited to a maximum
current of 100 mA, for a device total of 200 mA.
(3) The total current sourced by all I/Os must be limited to 160 mA (80 mA for P07-P00 and 80 mA for P17-P10).
6.6 I2C Interface Timing Requirements
over recommended operating free-air temperature range (unless otherwise noted) (see Figure 7-1)
MIN MAX UNIT
I2C BUS-STANDARD MODE
fscl I2C clock frequency 0 100 kHz
tsch I2C clock high time 4 us
tscl I2C clock low time 4.7 us
tsp I2C spike time 50 ns
tsds I2C serial-data setup time 250 ns
tsdh I2C serial-data hold time 0 ns
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 7
Product Folder Links: PCA9555

## Page 8

6.6 I2C Interface Timing Requirements (continued)
over recommended operating free-air temperature range (unless otherwise noted) (see Figure 7-1)
MIN MAX UNIT
ticr I2C input rise time 1000 ns
ticf I2C input fall time 300 ns
tocf I2C output fall time 10-pF to 400-pF bus 300 ns
tbuf I2C bus free time between stop and start 4.7 us
tsts I2C start or repeated start condition setup 4.7 us
tsth I2C start or repeated start condition hold 4 us
tsps I2C stop condition setup 4 us
tvd(data) Valid data time SCL low to SDA output valid 3.45 us
tvd(ack) Valid data time of ACK condition ACK signal from SCL low to
SDA (out) low 3.45 us
Cb (1) I2C bus capacitive load 400 pF
I2C BUS-FAST MODE
fscl I2C clock frequency 0 400 kHz
tsch I2C clock high time 0.6 us
tscl I2C clock low time 1.3 us
tsp I2C spike time 50 ns
tsds I2C serial-data setup time 100 ns
tsdh I2C serial-data hold time 0 ns
ticr I2C input rise time 20 300 ns
ticf I2C input fall time 20 x (VCC /
5.5 V) 300 ns
tocf I2C output fall time 10-pF to 400-pF bus 20 x (VCC /
5.5 V) 300 ns
tbuf I2C bus free time between stop and start 1.3 us
tsts I2C start or repeated start condition setup 0.6 us
tsth I2C start or repeated start condition hold 0.6 us
tsps I2C stop condition setup 0.6 us
tvd(data) Valid data time SCL low to SDA output valid 0.9 us
tvd(ack) Valid data time of ACK condition ACK signal from SCL low to
SDA (out) low 0.9 us
Cb (1) I2C bus capacitive load 400 pF
(1) C b = total capacitance of one bus line in pF.
6.7 Switching Characteristics
over recommended operating free-air temperature range, CL <= 100 pF (unless otherwise noted) (see Figure 7-1 and Figure
7-2)
PARAMETER FROM
(INPUT)
TO
(OUTPUT) MIN MAX UNIT
tiv Interrupt valid time P port INT 4 us
tir Interrupt reset delay time SCL INT 4 us
tpv Output data valid SCL P port 200 ns
tps Input data setup time P port SCL 150 ns
tph Input data hold time P port SCL 1 us
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
8 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 9

6.8 Typical Characteristics
TA = 25 deg C (unless otherwise noted)
TA - Temperature ( deg C)
ICC - Supply Current (uA)
-40 -15 10 35 60 85
0
4
8
12
16
20
24
28
32
36
40
D001
Vcc = 1.65 V
Vcc = 1.8 V
Vcc = 2.5 V
Vcc = 3.3 V
Vcc = 3.6 V
Vcc = 5 V
Vcc = 5.5V
Figure 6-1. Supply Current vs Temperature for Different Supply
Voltage (VCC)
TA - Temperature ( deg C)
ICC - Supply Current (uA)
-40 -15 10 35 60 85
0.2
0.4
0.6
0.8
1
1.2
1.4
1.6
1.8
2
2.2
D002
Vcc = 1.65 V
Vcc = 1.8 V
Vcc = 2.5 V
Vcc = 3.3 V
Vcc = 3.6 V
Vcc = 5 V
Vcc = 5.5V
Figure 6-2. Standby Supply Current vs Temperature for
Different Supply Voltage (VCC)
VCC - Supply Voltage (V)
ICC - Supply Current (uA)
1.5 2 2.5 3 3.5 4 4.5 5 5.5
0
5
10
15
20
25
30
D003
-40qC
25qC
85qC
Figure 6-3. Supply Current vs Supply Voltage for Different
Temperature (TA)
VOL - Output Low Voltage (V)
IOL - Sink Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
5
10
15
20
25
30
D004
VCC = 1.65 V
-40qC
25qC
85qC
Figure 6-4. I/O Sink Current vs Output Low Voltage for Different
Temperature (TA) for VCC = 1.65 V
VOL - Output Low Voltage (V)
IOL - Sink Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
5
10
15
20
25
30
35
D005
VCC = 1.8 V
-40qC
25qC
85qC
Figure 6-5. I/O Sink Current vs Output Low Voltage for Different
Temperature (TA) for VCC = 1.8 V
VOL - Output Low Voltage (V)
IOL - Sink Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
D006
VCC = 2.5 V
-40qC
25qC
85qC
Figure 6-6. I/O Sink Current vs Output Low Voltage for Different
Temperature (TA) for VCC = 2.5 V
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 9
Product Folder Links: PCA9555

## Page 10

6.8 Typical Characteristics (continued)
TA = 25 deg C (unless otherwise noted)
VOL - Output Low Voltage (V)
IOL - Sink Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
70
D007
VCC = 3.3 V
-40qC
25qC
85qC
Figure 6-7. I/O Sink Current vs Output Low Voltage for Different
Temperature (TA) for VCC = 3.3 V
VOL - Output Low Voltage (V)
IOL - Sink Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
70
80
D009
VCC = 5 V
-40qC
25qC
85qC
Figure 6-8. I/O Sink Current vs Output Low Voltage for Different
Temperature (TA) for VCC = 5 V
VOL - Output Low Voltage (V)
IOL - Sink Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
70
80
90
D010
VCC = 5.5 V
-40qC
25qC
85qC
Figure 6-9. I/O Sink Current vs Output Low Voltage for Different
Temperature (TA) for VCC = 5.5 V
TA - Temperature ( deg C)
VOL - Output Low Voltage (V)
-40 -15 10 35 60 85
0
50
100
150
200
250
300
D011
1.8 V, 1 mA
1.8 V, 10 mA
3.3 V, 1mA
3.3 V, 10 mA
5 V, 1 mA
5 V, 10 mA
Figure 6-10. I/O Low Voltage vs Temperature for Different VCC
and IOL
VCC-VOH - Output High Voltage (V)
IOH - Source Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
5
10
15
20
D012
VCC = 1.65 V
-40qC
25qC
85qC
Figure 6-11. I/O Source Current vs Output High Voltage for
Different Temperature (TA) for VCC = 1.65 V
VCC-VOH - Output High Voltage (V)
IOH - Source Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
5
10
15
20
25
D013
VCC = 1.8 V
-40qC
25qC
85qC
Figure 6-12. I/O Source Current vs Output High Voltage for
Different Temperature (TA) for VCC = 1.8 V
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
10 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 11

6.8 Typical Characteristics (continued)
TA = 25 deg C (unless otherwise noted)
VCC-VOH - Output High Voltage (V)
IOH - Source Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
5
10
15
20
25
30
35
40
D014
VCC = 2.5 V
-40qC
25qC
85qC
Figure 6-13. I/O Source Current vs Output High Voltage for
Different Temperature (TA) for VCC = 2.5 V
VCC-VOH - Output High Voltage (V)
IOH - Source Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
D015
VCC = 3.3 V
-40qC
25qC
85qC
Figure 6-14. I/O Source Current vs Output High Voltage for
Different Temperature (TA) for VCC = 3.3 V
VCC-VOH - Output High Voltage (V)
IOH - Source Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
70
D016
VCC = 5 V
-40qC
25qC
85qC
Figure 6-15. I/O Source Current vs Output High Voltage for
Different Temperature (TA) for VCC = 5 V
VCC-VOH - Output High Voltage (V)
IOH - Source Current (mA)
0 0.1 0.2 0.3 0.4 0.5 0.6 0.7
0
10
20
30
40
50
60
70
80
D017
VCC = 5.5 V
-40qC
25qC
85qC
Figure 6-16. I/O Source Current vs Output High Voltage for
Different Temperature (TA) for VCC = 5.5 V
TA - Temperature ( deg C)
VCC-VOH - I/O High Voltage (mV)
-40 -15 10 35 60 85
50
100
150
200
250
300
350
400
D018
1.65 V, 10 mA
2.5 V, 10 mA
3.6 V, 10 mA
5 V, 10 mA
5.5 V, 10 mA
Figure 6-17. VCC - VOH Voltage vs Temperature for Different VCC
TA - Temperature ( deg C)
Delta ICC (uA)
-40 -15 10 35 60 85
0
3
6
9
12
15
18
D019
1.65 V
1.8 V
2.5 V
3.3 V
5 V
5.5 V Figure 6-18.  ICC vs Temperature for Different VCC (VI = VCC -
0.6 V)
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 11
Product Folder Links: PCA9555

## Page 12

7 Parameter Measurement Information
RL = 1 k/c87
VCC
CL = 50 pF
tbuf
ticr
tsth tsds
tsdh
ticf
ticr
tscl tsch
tststPHL
tPLH
0.3 /c215VCC
Stop
Condition
tsps
Repeat
Start
ConditionStart or
Repeat
Start
Condition
SCL
SDA
Start
Condition
(S)
Address
Bit 7
(MSB)
Data
Bit 10
(LSB)
Stop
Condition
(P)
Three Bytes for Complete
Device Programming
SDA LOAD CONFIGURA TION
VOLTAGE WAVEFORMS
ticf
Stop
Condition
(P)
tsp
DUT SDA
0.7 /c215VCC
0.3 /c215VCC
0.7 /c215VCC
R/W
Bit 0
(LSB)
ACK
(A)
Data
Bit 07
(MSB)
Address
Bit 1
Address
Bit 6
BYTE DESCRIPTION
1 I 2C address
2, 3 P-port data
A. C L includes probe and jig capacitance.
B. All inputs are supplied by generators having the following characteristics: PRR <= 10 MHz, Z O = 50 , tr/tf <= 30 ns.
C. All parameters and waveforms are not applicable to all devices.
Figure 7-1. I2C Interface Load Circuit And Voltage Waveforms
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
12 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 13

P00 A
0.7 /c215VCC
0.3 /c215VCC
SCL P17
tpv
(see Note A)
Slave
ACK
Unstable
Data
Last Stable Bit
SDA
Pn
Pn
WRITE MODE (R/W = 0)
P00 A
0.7 /c215VCC
0.3 /c215VCC
SCL P17
0.7 /c215VCC
0.3 /c215VCC
tps tph
READ MODE (R/W = 1)
DUT
GND
CL = 100 pF
P-PORT LOAD CONFIGURATION
Pn
A. C L includes probe and jig capacitance.
B. t pv is measured from 0.7 x VCC on SCL to 50% I/O (Pn) output.
C. All inputs are supplied by generators having the following characteristics: PRR <= 10 MHz, Z O = 50 , tr/tf <= 30 ns.
D. The outputs are measured one at a time, with one transition per measurement.
E. All parameters and waveforms are not applicable to all devices.
Figure 7-2. P-Port Load Circuit And Voltage Waveforms
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 13
Product Folder Links: PCA9555

## Page 14

8 Detailed Description
8.1 Overview
The system master can reset the PCA9555 in the event of a timeout or other improper operation by utilizing
the power-on reset feature, which puts the registers in their default state and initializes the I 2C/SMBus state
machine.
The PCA9555 open-drain interrupt ( INT) output is activated when any input state differs from its corresponding
Input Port register state and is used to indicate to the system master that an input state has changed.
INT can be connected to the interrupt input of a microcontroller. By sending an interrupt signal on this line, the
remote I/O can inform the microcontroller if there is incoming data on its ports without having to communicate via
the I2C bus. Thus, the PCA9555 can remain a simple slave device.
The device outputs (latched) have high-current drive capability for directly driving LEDs.
Although pin-to-pin and I 2C-address is compatible with the PCF8575, software changes are required due to the
enhancements.
The PCA9555 is identical to the PCA9535, except for the inclusion of the internal I/O pullup resistor, which pulls
the I/O to a default high when configured as an input and undriven.
Three hardware pins (A0, A1, and A2) are used to program and vary the fixed I 2C address and allow up to
eight devices to share the same I 2C bus or SMBus. The fixed I 2C address of the PCA9555 is the same as
the PCF8575, PCF8575C, and PCF8574, allowing up to eight of these devices in any combination to share the
same I2C bus or SMBus.
8.2 Functional Block Diagram
22
I/O
Port
P17-P10
Shift
Register 16 Bits
LP FilterInterrupt
Logic
Input
Filter
23
Power-On
Reset
Read Pulse
Write Pulse
PCA9555
3
2
21
1
24
12GND
VCC
SDA
SCL
A2
A1
A0
INT
I2C Bus
Control
P07-P00
A. Pin numbers shown are for DB, DBQ, DGV, DW, and PW packages.
B. All I/Os are set to inputs at reset.
Figure 8-1. Logic Diagram
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
14 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 15

VCC
CLK
D Q
FF
Configuration
Register
Data From
Shift Register
Data From
Shift Register
QWrite Configuration
Pulse
CLK
D Q
FF
QWrite Pulse
Output Port
Register
100 k
Q1
Q2
GND
I/O Pin
Output Port
Register Data
CLK
D Q
FF
Q
Input Port
Register
Read Pulse
CLK
D Q
FF
Q
Polarity Inversion
Register
Write Polarity
Pulse
Input Port
Register Data
Polarity
Register Data
To INT
Data From
Shift Register
A. At power-on reset, all registers return to default values.
Figure 8-2. Simplified Schematic Of P-Port I/Os
8.3 Device Features
8.3.1 Power-On Reset (POR)
When power (from 0 V) is applied to V CC, an internal power-on reset circuit holds the PCA9555 in a reset
condition until V CC has reached V POR. At that time, the reset condition is released, and the PCA9555 registers
and I2C-SMBus state machine initialize to their default states. After that, V CC must be lowered to below V PORF
and back up to the operating voltage for a power-reset cycle.
8.3.2 I/O Port
When an I/O is configured as an input, FETs Q1 and Q2 (in Figure 8-2) are off, creating a high-impedance input.
The input voltage may be raised above VCC to a maximum of 5.5 V.
If the I/O is configured as an output, Q1 or Q2 is enabled, depending on the state of the Output Port register.
In this case, there are low-impedance paths between the I/O pin and either V CC or GND. The external voltage
applied to this I/O pin should not exceed the recommended levels for proper operation.
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 15
Product Folder Links: PCA9555

## Page 16

8.4 Device Functional Modes
8.4.1 Interrupt ( INT) Output
An interrupt is generated by any rising or falling edge of the port inputs in the input mode. After time, t iv,
the signal INT is valid. Resetting the interrupt circuit is achieved when data on the port is changed to the
original setting, data is read from the port that generated the interrupt. Resetting occurs in the read mode at the
acknowledge (ACK) or not acknowledge (NACK) bit after the rising edge of the SCL signal.
Interrupts that occur during the ACK or NACK clock pulse can be lost (or be very short) due to the resetting
of the interrupt during this pulse. Each change of the I/Os after resetting is detected and is transmitted as INT.
Writing to another device does not affect the interrupt circuit, and a pin configured as an output cannot cause
an interrupt. Changing an I/O from an output to an input may cause a false interrupt to occur, if the state of the
pin does not match the contents of the Input Port register. Because each 8-pin port is read independently, the
interrupt caused by port 0 is not cleared by a read of port 1 or vice versa.
The INT output has an open-drain structure and requires pullup resistor to VCC.
8.4.1.1 Interrupt Errata
8.4.1.1.1 INT Description
The INT will be improperly de-asserted if the following two conditions occur:
1. The last I 2C command byte (register pointer) written to the device was 00h.
Note
This generally means the last operation with the device was a Read of the input register. However,
the command byte may have been written with 00h without ever going on to read the input register.
After reading from the device, if no other command byte written, it will remain 00h.
2. Any other slave device on the I 2C bus acknowledges an address byte with the R/W bit set high
8.4.1.1.2 System Impact
Can cause improper interrupt handling as the Master will see the interrupt as being cleared.
8.4.1.1.3 System Workaround
Minor software change: User must change command byte to something besides 00h after a Read operation to
the PCA9555 device or before reading from another slave device.
Note
Software change will be compatible with other versions (competition and TI redesigns) of this device.
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
16 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 17

8.5 Programming
8.5.1 I2C Interface
The bidirectional I 2C bus consists of the serial clock (SCL) and serial data (SDA) lines. Both lines must be
connected to a positive supply via a pullup resistor when connected to the output stages of a device. Data
transfer may be initiated only when the bus is not busy.
I2C communication with this device is initiated by a master sending a Start condition, a high-to-low transition on
the SDA input/output while the SCL input is high (see Figure 8-3). After the Start condition, the device address
byte is sent, MSB first, including the data direction bit (R/W). This device does not respond to the general call
address.
After receiving the valid address byte, this device responds with an ACK, a low on the SDA input/output during
the high of the ACK-related clock pulse. The address inputs (A0-A2) of the slave device must not be changed
between the Start and Stop conditions.
On the I2C bus, only one data bit is transferred during each clock pulse. The data on the SDA line must remain
stable during the high pulse of the clock period, as changes in the data line at this time are interpreted as control
commands (Start or Stop) (see Figure 8-4).
A Stop condition, a low-to-high transition on the SDA input/output while the SCL input is high, is sent by the
master (see Figure 8-3).
Any number of data bytes can be transferred from the transmitter to the receiver between the Start and the Stop
conditions. Each byte of eight bits is followed by one ACK bit. The transmitter must release the SDA line before
the receiver can send an ACK bit. The device that acknowledges must pull down the SDA line during the ACK
clock pulse so that the SDA line is stable low during the high pulse of the ACK-related clock period (see Figure
8-5). When a slave receiver is addressed, it must generate an ACK after each byte is received. Similarly, the
master must generate an ACK after each byte that it receives from the slave transmitter. Setup and hold times
must be met to ensure proper operation.
A master receiver signals an end of data to the slave transmitter by not generating an acknowledge (NACK) after
the last byte has been clocked out of the slave. This is done by the master receiver by holding the SDA line high.
In this event, the transmitter must release the data line to enable the master to generate a Stop condition.
SDA
SCL
Start Condition
S
Stop Condition
P
Figure 8-3. Definition Of Start And Stop Conditions
SDA
SCL
Data Line
Stable;
Data Valid
Change
of Data
Allowed
Figure 8-4. Bit Transfer
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 17
Product Folder Links: PCA9555

## Page 18

Data Output
by Transmitter
SCL From
Master
Start
Condition
S
1 2 8 9
Data Output
by Receiver
Clock Pulse for
Acknowledgment
NACK
ACK
Figure 8-5. Acknowledgment On I2C Bus
8.5.2 Register Map
Table 8-1. Interface Definition
BYTE
BIT
7 (MSB) 6 5 4 3 2 1 0 (LSB)
I2C slave address L H L L A2 A1 A0 R/ W
P0x I/O data bus P07 P06 P05 P04 P03 P02 P01 P00
P1x I/O data bus P17 P16 P15 P14 P13 P12 P11 P10
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
18 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 19

8.5.2.1 Device Address
Figure 8-6 shows the address byte of the PCA9555.
0 1 0 0 A1 A2 A0
Slave Address
R/W
Fixed Programmable
Figure 8-6. PCA9555 Address
Table 8-2. Address Reference
INPUTS
I2C BUS SLAVE ADDRESS
A2 A1 A0
L L L 32 (decimal), 20 (hexadecimal)
L L H 33 (decimal), 21 (hexadecimal)
L H L 34 (decimal), 22 (hexadecimal)
L H H 35 (decimal), 23 (hexadecimal)
H L L 36 (decimal), 24 (hexadecimal)
H L H 37 (decimal), 25 (hexadecimal)
H H L 38 (decimal), 26 (hexadecimal)
H H H 39 (decimal), 27 (hexadecimal)
The last bit of the slave address defines the operation (read or write) to be performed. A high (1) selects a read
operation, while a low (0) selects a write operation.
8.5.2.2 Control Register And Command Byte
Following the successful acknowledgment of the address byte, the bus master sends a command byte that is
stored in the control register in the PCA9555. Three bits of this data byte state the operation (read or write) and
the internal register (input, output, polarity inversion, or configuration) that will be affected. This register can be
written or read through the I2C bus. The command byte is sent only during a write transmission.
Once a command byte has been sent, the register that was addressed continues to be accessed by reads until a
new command byte has been sent.
Figure 8-7. Control Register Bits
7 6 5 4 3 2 1 0
0 0 0 0 0 B2 B1 B0
Table 8-3. Command Byte
CONTROL REGISTER BITS COMMAND
BYTE (HEX) REGISTER PROTOCOL POWER-UP
DEFAULTB2 B1 B0
0 0 0 0x00 Input Port 0 Read byte xxxx xxxx
0 0 1 0x01 Input Port 1 Read byte xxxx xxxx
0 1 0 0x02 Output Port 0 Read/write byte 1111 1111
0 1 1 0x03 Output Port 1 Read/write byte 1111 1111
1 0 0 0x04 Polarity Inversion Port 0 Read/write byte 0000 0000
1 0 1 0x05 Polarity Inversion Port 1 Read/write byte 0000 0000
1 1 0 0x06 Configuration Port 0 Read/write byte 1111 1111
1 1 1 0x07 Configuration Port 1 Read/write byte 1111 1111
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 19
Product Folder Links: PCA9555

## Page 20

8.5.2.3 Register Descriptions
The Input Port registers (registers 0 and 1) reflect the incoming logic levels of the pins, regardless of whether
the pin is defined as an input or an output by the Configuration register. It only acts on read operation. Writes to
these registers have no effect. The default value, X, is determined by the externally applied logic level.
Before a read operation, a write transmission is sent with the command byte to indicate to the I 2C device that the
Input Port register will be accessed next.
Table 8-4. Registers 0 And 1 (Input Port Registers)
Bit I0.7 I0.6 I0.5 I0.4 I0.3 I0.2 I0.1 I0.0
Default X X X X X X X X
Bit I1.7 I1.6 I1.5 I1.4 I1.3 I1.2 I1.1 I1.0
Default X X X X X X X X
The Output Port registers (registers 2 and 3) show the outgoing logic levels of the pins defined as outputs by the
Configuration register. Bit values in this register have no effect on pins defined as inputs. In turn, reads from this
register reflect the value that is in the flip-flop controlling the output selection, not the actual pin value.
Table 8-5. Registers 2 And 3 (Output Port Registers)
Bit O0.7 O0.6 O0.5 O0.4 O0.3 O0.2 O0.1 O0.0
Default 1 1 1 1 1 1 1 1
Bit O1.7 O1.6 O1.5 O1.4 O1.3 O1.2 O1.1 O1.0
Default 1 1 1 1 1 1 1 1
The Polarity Inversion registers (registers 4 and 5) allow polarity inversion of pins defined as inputs by the
Configuration register. If a bit in this register is set (written with 1), the corresponding port pin's polarity is
inverted. If a bit in this register is cleared (written with a 0), the corresponding port pin's original polarity is
retained.
Table 8-6. Registers 4 And 5 (Polarity Inversion Registers)
Bit N0.7 N0.6 N0.5 N0.4 N0.3 N0.2 N0.1 N0.0
Default 0 0 0 0 0 0 0 0
Bit N1.7 N1.6 N1.5 N1.4 N1.3 N1.2 N1.1 N1.0
Default 0 0 0 0 0 0 0 0
The Configuration registers (registers 6 and 7) configure the directions of the I/O pins. If a bit in this register is
set to 1, the corresponding port pin is enabled as an input with a high-impedance output driver. If a bit in this
register is cleared to 0, the corresponding port pin is enabled as an output.
Table 8-7. Registers 6 And 7 (Configuration Registers)
Bit C0.7 C0.6 C0.5 C0.4 C0.3 C0.2 C0.1 C0.0
Default 1 1 1 1 1 1 1 1
Bit C1.7 C1.6 C1.5 C1.4 C1.3 C1.2 C1.1 C1.0
Default 1 1 1 1 1 1 1 1
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
20 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 21

8.5.2.4 Bus Transactions
Data is exchanged between the master and the PCA9555 through write and read commands.
8.5.2.4.1 Writes
Data is transmitted to the PCA9555 by sending the device address and setting the least-significant bit to a
logic 0 (see Figure 8-6 for device address). The command byte is sent after the address and determines which
register receives the data that follows the command byte.
The eight registers within the PCA9555 are configured to operate as four register pairs. The four pairs are input
ports, output ports, polarity inversion ports, and configuration ports. After sending data to one register, the next
data byte is sent to the other register in the pair (see Figure 8-8 and Figure 8-9). For example, if the first byte is
sent to output port (register 3), the next byte is stored in Output Port 0 (register 2).
There is no limitation on the number of data bytes sent in one write transmission. In this way, each 8-bit register
may be updated independently of the other registers.
1 2SCL 3 4 5 6 7 8
SDA A A AData 0
R/W
tpv
9
00 0 0 0 0 0 1 0.7 0.0 Data 11.7 1.0 AS 0 1 0 0 A2 A1 A0 0
tpv
P
Slave Address Command Byte Data to Port 0 Data to Port 1
Start Condition Acknowledge
From Slave
Write to Port
Data Out from Port 1
Data Out from Port 0
Data Valid
Acknowledge
From Slave
Acknowledge
From Slave
Figure 8-8. Write To Output Port Registers
1 2SCL 3 4 5 6 7 8
SDA A A AData 0
D ata to Register
R/W
9
00 0 0 0 0 1 1 MSB LSB Data 1MSB LSB A
D ata to Register
S 0 1 0 0 A2 A1 A0 0
1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7 8 9 1 2 3 4 5
P
Acknowledge
From Slave
Acknowledge
From Slave
Start Condition
Command ByteSlave Address
Acknowledge
From Slave
Figure 8-9. Write To Configuration Registers
8.5.2.4.2 Reads
The bus master first must send the PCA9555 address with the least-significant bit set to a logic 0 (see Figure
8-6 for device address). The command byte is sent after the address and determines which register is accessed.
After a restart, the device address is sent again, but this time, the least-significant bit is set to a logic 1. Data
from the register defined by the command byte then is sent by the PCA9555 (see Figure 8-10 through Figure
8-12).
After a restart, the value of the register defined by the command byte matches the register being accessed when
the restart occurred. For example, if the command byte references Input Port 1 before the restart, and the restart
occurs when Input Port 0 is being read, the stored command byte changes to reference Input Port 0. The original
command byte is forgotten. If a subsequent restart occurs, Input Port 0 is read first. Data is clocked into the
register on the rising edge of the ACK clock pulse. After the first byte is read, additional bytes may be read, but
the data now reflect the information in the other register in the pair. For example, if Input Port 1 is read, the next
byte read is Input Port 0.
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 21
Product Folder Links: PCA9555

## Page 22

Data is clocked into the register on the rising edge of the ACK clock pulse. There is no limitation on the number
of data bytes received in one read transmission, but when the final byte is received, the bus master must not
acknowledge the data.
0 0 A2 A1 A00 10 0 A2 A1 A00 1S 0 A A A
R/W
A
PNA
S
R/W
1 MSB LSB
MSB LSB
Slave Address
Acknowledge
From Slave
Command Byte
Data From Upper
or Lower Byte
of Register
Last Byte
Data
Acknowledge
From Slave
Acknowledge
From SlaveSlave Address
Data From Lower
or Upper Byte
of Register
First Byte
Data
No Acknowledge
From Master
Acknowledge
From Master
At this moment, master transmitter
becomes master receiver , and
slave-receiver becomes
slave-transmitter.
Figure 8-10. Read From Register
1 2 3 4 5 6 7 8 9
S 0 1 0 0 A2 A1 A0 1 A 7 6 5 4 3 2 1 0 A
I0.x
7 6 5 4 3 2 1 0 A
I1.x
7 6 5 4 3 2 1 0 A
I0.x
7 6 5 4 3 2 1 0 1
I1.x
P
R/W
SCL
SDA
INT
Read From Port 0
Data Into Port 0
Read From Port 1
Data Into Port 1
Acknowledge
From Master
Acknowledge
From Slave
Acknowledge
From Master
Acknowledge
From Master
No Acknowledge
From Master
tiv tir
A. Transfer of data can be stopped at any time by a Stop condition. When this occurs, data present at the latest acknowledge phase is valid
(output mode). It is assumed that the command byte previously has been set to 00 (read Input Port register).
B. This figure eliminates the command byte transfer, a restart, and slave address call between the initial slave address call and actual data
transfer from the P port (see Figure 8-10 for these details).
Figure 8-11. Read Input Port Register, Scenario 1
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
22 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 23

1 2 3 4 5 6 7 8 9
S 0 1 0 0 A2 A1 A0 1 A A
I0.x
A
I1.x
A
I0.x
1
I1.x
P
R/W
SCL
SDAINT
tph
00 10 03 12
tps
tph tps
11 12
Read From Port 0
Data Into Port 0
Read From Port 1
Data Into Port 1
Data 02Data 01Data 00 Data 03
DataDataData 10
Acknowledge
From Slave
Acknowledge
From Master
Acknowledge
From Master
Acknowledge
From Master
No Acknowledge
From Master
tiv tir
A. Transfer of data can be stopped at any time by a Stop condition. When this occurs, data present at the latest acknowledge phase is valid
(output mode). It is assumed that the command byte previously has been set to 00 (read Input Port register).
B. This figure eliminates the command byte transfer, a restart, and slave address call between the initial slave address call and actual data
transfer from the P port (see Figure 8-10 for these details).
Figure 8-12. Read Input Port Register, Scenario 2
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 23
Product Folder Links: PCA9555

## Page 24

9 Application Information Disclaimer
Note
Information in the following applications sections is not part of the TI component specification,
and TI does not warrant its accuracy or completeness. TI's customers are responsible for
determining suitability of components for their purposes, as well as validating and testing their design
implementation to confirm system functionality.
9.1 Application Information
9.1.1 Typical Application
Figure 9-1 shows an application in which the PCA9555 can be used.
P00
P01
P02
P03
P04
P05
A2
A1
A0
A
B
P06
P07
P10
P11
P12
P13
P14
P15
P16
P17
VCC
VCC
VCC
(5 V)
Controlled Switch
(e.g., CBT Device)
GND
INT
SDA
SCL
10 k 10 k 10 k 10 k 2 k
INT
Subsystem 1
(e.g., Temperature
Sensor)
Subsystem 2
(e.g., Counter)
PCA9555
SDA
SCL
INT
GND
Keypad
ALARM
RESET
ENABLE
Subsystem 3
(e.g., Alarm)
Master
Controller
4
5
6
7
8
9
10
11
13
14
15
16
17
18
19
20
22
23
1
3
2
21
12
24
VCC
A. Device address is configured as 0100100 for this example.
B. P00, P02, and P03 are configured as outputs.
C. P01, P04-P07, and P10-P17 are configured as inputs.
D. Pin numbers shown are for DB, DBQ, DGV, DW, and PW packages.
Figure 9-1. Typical Application
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
24 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 25

9.1.1.1 Design Requirements
For this design example, use the parameters shown in Table 9-1.
Table 9-1. Design Parameters
DESIGN PARAMETER EXAMPLE VALUE
I2C and Subsystem Voltage (VCC) 5 V
Output current rating, P-port sinking (IOL) 25 mA
I2C bus clock (SCL) speed 400 kHz
9.1.1.2 Design Requirements
9.1.1.2.1 Minimizing ICC When I/O Is Used To Control Led
When an I/O is used to control an LED, normally it is connected to V CC through a resistor as shown in Figure
9-1. Because the LED acts as a diode, when the LED is off, the I/O V IN is about 1.2 V less than V CC. The
ICC parameter in Electrical Characteristics shows how I CC increases as V IN becomes lower than V CC. For
battery-powered applications, it is essential that the voltage of I/O pins is greater than or equal to V CC when the
LED is off to minimize current consumption.
Figure 9-2 shows a high-value resistor in parallel with the LED. Figure 9-3 shows VCC less than the LED supply
voltage by at least 1.2 V. Both of these methods maintain the I/O V IN at or above V CC and prevent additional
supply current consumption when the LED is off.
VCC
Pn
100 k
LED
VCC
Figure 9-2. High-Value Resistor In Parallel With Led
VCC
3.3 V 5 V
LED
Pn
Figure 9-3. Device Supplied By Lower Voltage
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 25
Product Folder Links: PCA9555

## Page 26

9.1.1.3 Application Curves
Bus Capacitance (pF)
Maximum Pull-Up Resistance (k:)
0 50 100 150 200 250 300 350 400 450
0
5
10
15
20
25
D008
Standard-Mode
Fast-Mode
Standard-mode: fSCL = 100 kHz, tr = 1 us
Fast-mode: fSCL = 400 kHz, tr = 300 ns
Figure 9-4. Maximum Pull-Up Resistance (Rp(max))
vs Bus Capacitance (Cb)
Pull-Up Reference Voltage (V)
Minimum Pull-Up Resistance (k:)
0 0.5 1 1.5 2 2.5 3 3.5 4 4.5 5 5.5
0
0.2
0.4
0.6
0.8
1
1.2
1.4
1.6
1.8
D009
VDPUX > 2 V
VDUPX </= 2
VOL = 0.2 x VCC, IOL = 2 mA when VCC <= 2 V
VOL = 0.4 V, IOL = 3 mA when VCC > 2 V
Figure 9-5. Minimum Pull-Up Resistance (Rp(min))
vs Pull-up Reference Voltage (VCC)
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
26 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 27

10 Power Supply Recommendations
10.1 Power-On Reset Requirements
In the event of a glitch or data corruption, PCA9555 can be reset to its default conditions by using the power-on
reset feature. Power-on reset requires that the device go through a power cycle to be completely reset. This
reset also happens when the device is powered on for the first time in an application.
The two types of power-on reset are shown in Figure 10-1 and Figure 10-2.
VCC
Ramp-Up Re-Ramp-Up
Time to Re-Ramp
Time
Ramp-Down
VCC_RT VCC_RTVCC_FT
VCC_TRR_GND
Figure 10-1. VCC Is Lowered Below 0.2 V Or 0 V And Then Ramped Up To VCC
VCC
Ramp-Up
Time to Re-Ramp
Time
Ramp-Down
VIN drops below POR levels
VCC_RTVCC_FT
VCC_TRR_VPOR50
Figure 10-2. VCC Is Lowered Below The Por Threshold, Then Ramped Back Up To VCC
Table 10-1 specifies the performance of the power-on reset feature for PCA9555 for both types of power-on
reset.
Table 10-1. Recommended Supply Sequencing And Ramp Rates (1)
PARAMETER MIN TYP MAX UNIT
VCC_FT Fall rate See Figure 10-1 1 100 ms
VCC_RT Rise rate See Figure 10-1 0.01 100 ms
VCC_TRR_GND Time to re-ramp (when VCC drops to GND) See Figure 10-1 0.001 ms
VCC_TRR_POR50 Time to re-ramp (when VCC drops to VPOR_MIN - 50 mV) See Figure 10-2 0.001 ms
VCC_GH
Level that VCCP can glitch down to, but not cause a functional
disruption when VCCX_GW = 1 us See Figure 10-3 1.2 V
VCC_GW
Glitch width that will not cause a functional disruption when
VCCX_GH = 0.5 x VCCx
See Figure 10-3 us
VPORF Voltage trip point of POR on falling VCC 0.767 1.144 V
VPORR Voltage trip point of POR on rising VCC 1.033 1.428 V
(1) T A = -40 deg C to 85 deg C (unless otherwise noted)
Glitches in the power supply can also affect the power-on reset performance of this device. The glitch width
(VCC_GW) and height (V CC_GH) are dependent on each other. The bypass capacitance, source impedance, and
the device impedance are factors that affect power-on reset performance. Figure 10-3 and Table 10-1 provide
more information on how to measure these specifications.
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 27
Product Folder Links: PCA9555

## Page 28

VCC
Time
VCC_GH
VCC_GW
Figure 10-3. Glitch Width And Glitch Height
VPOR is critical to the power-on reset. V POR is the voltage level at which the reset condition is released and
all the registers and the I 2C/SMBus state machine are initialized to their default states. The value of V POR
differs based on the V CC being lowered to or from 0. Figure 10-4 and Table 10-1 provide more details on this
specification.
VCC
VPOR
VPORF
Time
POR
Time
Figure 10-4. VPOR
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
28 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 29

11 Layout
11.1 Layout Guidelines
For printed circuit board (PCB) layout of the PCA9555, common PCB layout practices must be followed, but
additional concerns related to high-speed data transfer such as matched impedances and differential pairs are
not a concern for I2C signal speeds.
In all PCB layouts, it is a best practice to avoid right angles in signal traces, to fan out signal traces away from
each other upon leaving the vicinity of an integrated circuit (IC), and to use thicker trace widths to carry higher
amounts of current that commonly pass through power and ground traces. By-pass and de-coupling capacitors
are commonly used to control the voltage on the V CC pin, using a larger capacitor to provide additional power
in the event of a short power supply glitch and a smaller capacitor to filter out high-frequency ripple. These
capacitors must be placed as close to the PCA9555 as possible. These best practices are shown in the Section
11.2.
For the layout example provided in the Section 11.2, it is possible to fabricate a PCB with only 2 layers by
using the top layer for signal routing and the bottom layer as a split plane for power (V CC) and ground (GND).
However, a 4 layer board is preferable for boards with higher density signal routing. On a 4 layer PCB, it
is common to route signals on the top and bottom layer, dedicate one internal layer to a ground plane, and
dedicate the other internal layer to a power plane. In a board layout using planes or split planes for power and
ground, vias are placed directly next to the surface mount component pad which needs to attach to V CC, or GND
and the via is connected electrically to the internal layer or the other side of the board. Vias are also used when
a signal trace needs to be routed to the opposite side of the board, but this technique is not demonstrated in the
Section 11.2.
11.2 Layout Example
Figure 11-1. PCA9555 Example Layout
www.ti.com
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021
Copyright (C) 2021 Texas Instruments Incorporated Submit Document Feedback 29
Product Folder Links: PCA9555

## Page 30

12 Device and Documentation Support
12.1 Receiving Notification of Documentation Updates
To receive notification of documentation updates, navigate to the device product folder on ti.com. In the upper
right corner, click on Alert me  to register and receive a weekly digest of any product information that has
changed. For change details, review the revision history included in any revised document.
12.2 Support Resources
TI E2E (TM) support forums  are an engineer's go-to source for fast, verified answers and design help - straight
from the experts. Search existing answers or ask your own question to get the quick design help you need.
Linked content is provided "AS IS" by the respective contributors. They do not constitute TI specifications and do
not necessarily reflect TI's views; see TI's Terms of Use.
12.3 Trademarks
TI E2E(TM) is a trademark of Texas Instruments.
All trademarks are the property of their respective owners.
12.4 Electrostatic Discharge Caution
This integrated circuit can be damaged by ESD. Texas Instruments recommends that all integrated circuits be handled
with appropriate precautions. Failure to observe proper handling and installation procedures can cause damage.
ESD damage can range from subtle performance degradation to complete device failure. Precision integrated circuits may
be more susceptible to damage because very small parametric changes could cause the device not to meet its published
specifications.
12.5 Glossary
TI Glossary This glossary lists and explains terms, acronyms, and definitions.
13 Mechanical, Packaging, and Orderable Information
The following pages include mechanical, packaging, and orderable information. This information is the most
current data available for the designated devices. This data is subject to change without notice and revision of
this document. For browser-based versions of this data sheet, refer to the left-hand navigation.
PCA9555
SCPS131J - AUGUST 2005 - REVISED MARCH 2021 www.ti.com
30 Submit Document Feedback Copyright (C) 2021 Texas Instruments Incorporated
Product Folder Links: PCA9555

## Page 31

PACKAGE OPTION ADDENDUM
www.ti.com 9-Nov-2025
PACKAGING INFORMATION
Orderable part number Status
(1)
Material type
(2)
Package | Pins Package qty | Carrier RoHS
(3)
Lead finish/
Ball material
(4)
MSL rating/
Peak reflow
(5)
Op temp ( deg C) Part marking
(6)
PCA9555DBQR Obsolete Production SSOP (DBQ) | 24 - - Call TI Call TI -40 to 85 PCA9555
PCA9555DBR Active Production SSOP (DB) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555DBR.A Active Production SSOP (DB) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555DBR.B Active Production SSOP (DB) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555DGVR Active Production TVSOP (DGV) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555DGVR.A Active Production TVSOP (DGV) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555DW Active Production SOIC (DW) | 24 25 | TUBE Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PCA9555
PCA9555DW.A Active Production SOIC (DW) | 24 25 | TUBE Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PCA9555
PCA9555DWR Active Production SOIC (DW) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PCA9555
PCA9555DWR.A Active Production SOIC (DW) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PCA9555
PCA9555PW Obsolete Production TSSOP (PW) | 24 - - Call TI Call TI -40 to 85 PD9555
PCA9555PWR Active Production TSSOP (PW) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555PWR.A Active Production TSSOP (PW) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555PWR.B Active Production TSSOP (PW) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555PWRG4 Active Production TSSOP (PW) | 24 2000 | LARGE T&R Yes NIPDAU Level-1-260C-UNLIM -40 to 85 PD9555
PCA9555RGER Active Production VQFN (RGE) | 24 3000 | LARGE T&R Yes NIPDAU Level-2-260C-1 YEAR -40 to 85 PD9555
PCA9555RGER.A Active Production VQFN (RGE) | 24 3000 | LARGE T&R Yes NIPDAU Level-2-260C-1 YEAR -40 to 85 PD9555
PCA9555RGER.B Active Production VQFN (RGE) | 24 3000 | LARGE T&R Yes NIPDAU Level-2-260C-1 YEAR -40 to 85 PD9555
PCA9555RGERG4 Active Production VQFN (RGE) | 24 3000 | LARGE T&R Yes NIPDAU Level-2-260C-1 YEAR -40 to 85 PD9555

(1) Status:  For more details on status, see our product life cycle.

(2) Material type:  When designated, preproduction parts are prototypes/experimental devices, and are not yet approved or released for full production. Testing and final process, including without limitation quality assurance,
reliability performance testing, and/or process qualification, may not yet be complete, and this item is subject to further changes or possible discontinuation. If available for ordering, purchases will be subject to an additional
waiver at checkout, and are intended for early internal evaluation purposes only. These items are sold without warranties of any kind.

(3) RoHS values:  Yes, No, RoHS Exempt. See the TI RoHS Statement for additional information and value definition.

(4) Lead finish/Ball material:  Parts may have multiple material finish options. Finish options are separated by a vertical ruled line. Lead finish/Ball material values may wrap to two lines if the finish value exceeds the maximum
column width.
Addendum-Page 1

## Page 32

PACKAGE OPTION ADDENDUM
www.ti.com 9-Nov-2025

(5) MSL rating/Peak reflow:  The moisture sensitivity level ratings and peak solder (reflow) temperatures. In the event that a part has multiple moisture sensitivity ratings, only the lowest level per JEDEC standards is shown.
Refer to the shipping label for the actual reflow temperature that will be used to mount the part to the printed circuit board.

(6) Part marking:  There may be an additional marking, which relates to the logo, the lot trace code information, or the environmental category of the part.

Multiple part markings will be inside parentheses. Only one part marking contained in parentheses and separated by a "~" will appear on a part. If a line is indented then it is a continuation of the previous line and the two
combined represent the entire part marking for that device.

Important Information and Disclaimer:The information provided on this page represents TI's knowledge and belief as of the date that it is provided. TI bases its knowledge and belief on information provided by third parties, and
makes no representation or warranty as to the accuracy of such information. Efforts are underway to better integrate information from third parties. TI has taken and continues to take reasonable steps to provide representative
and accurate information but may not have conducted destructive testing or chemical analysis on incoming materials and chemicals. TI and TI suppliers consider certain information to be proprietary, and thus CAS numbers
and other limited information may not be available for release.

In no event shall TI's liability arising out of such information exceed the total purchase price of the TI part(s) at issue in this document sold by TI to Customer on an annual basis.

Addendum-Page 2

## Page 33

PACKAGE MATERIALS INFORMATION

www.ti.com 9-Oct-2025
TAPE AND REEL INFORMATION
Reel Width (W1)
REEL DIMENSIONS
A0B0K0WDimension designed to accommodate the component lengthDimension designed to accommodate the component thicknessOverall width of the carrier tapePitch between successive cavity centersDimension designed to accommodate the component width
TAPE DIMENSIONSK0 P1B0WA0Cavity
QUADRANT ASSIGNMENTS FOR PIN 1 ORIENTATION IN TAPE
Pocket QuadrantsSprocket HolesQ1Q1Q2Q2Q3Q3Q4Q4User Direction of Feed
P1ReelDiameter

*All dimensions are nominal
Device Package
Type
Package
Drawing
Pins SPQ Reel
Diameter
(mm)
Reel
Width
W1 (mm)
A0
(mm)
B0
(mm)
K0
(mm)
P1
(mm)
W
(mm)
Pin1
Quadrant
PCA9555DBR SSOP DB 24 2000 330.0 16.4 8.2 8.8 2.5 12.0 16.0 Q1
PCA9555DGVR TVSOP DGV 24 2000 330.0 12.4 6.9 5.6 1.6 8.0 12.0 Q1
PCA9555DWR SOIC DW 24 2000 330.0 24.4 10.75 15.7 2.7 12.0 24.0 Q1
PCA9555PWR TSSOP PW 24 2000 330.0 16.4 6.95 8.3 1.6 8.0 16.0 Q1
PCA9555RGER VQFN RGE 24 3000 330.0 12.4 4.25 4.25 1.15 8.0 12.0 Q2
Pack Materials-Page 1

## Page 34

PACKAGE MATERIALS INFORMATION

www.ti.com 9-Oct-2025
TAPE AND REEL BOX DIMENSIONS
Width (mm)
W LH

*All dimensions are nominal
Device Package Type Package Drawing Pins SPQ Length (mm) Width (mm) Height (mm)
PCA9555DBR SSOP DB 24 2000 353.0 353.0 32.0
PCA9555DGVR TVSOP DGV 24 2000 353.0 353.0 32.0
PCA9555DWR SOIC DW 24 2000 350.0 350.0 43.0
PCA9555PWR TSSOP PW 24 2000 353.0 353.0 32.0
PCA9555RGER VQFN RGE 24 3000 346.0 346.0 33.0
Pack Materials-Page 2

## Page 35

PACKAGE MATERIALS INFORMATION

www.ti.com 9-Oct-2025
TUBE

L - Tube length
T - Tube
height
W - Tube
width
B - Alignment groove width

*All dimensions are nominal
Device Package Name Package Type Pins SPQ L (mm) W (mm) T (um) B (mm)
PCA9555DW DW SOIC 24 25 506.98 12.7 4826 6.6
PCA9555DW.A DW SOIC 24 25 506.98 12.7 4826 6.6
Pack Materials-Page 3

## Page 36

[No extractable text found on this page.]

## Page 37

www.ti.com
PACKAGE OUTLINE
C
22X 0.4
2X
4.4
24X 0.23
0.13
6.6
6.2 TYP
SEATING
PLANE
0.15
0.05
0.25
GAGE PLANE
0 -8
1.2 MAX
B 4.5
4.3
NOTE 4
A
5.1
4.9
NOTE 3
0.75
0.50
(0.15) TYP
TVSOP - 1.2 mm max heightDGV0024A
SMALL OUTLINE PACKAGE
4229221/A   12/2022
1
12
13
24
0.07 C A B
PIN 1 INDEX
AREA
SEE DETAIL  A
0.08 C
NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing
    per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. This dimension does not include mold flash, protrusions, or gate burrs. Mold flash, protrusions, or gate burrs shall not
    exceed 0.15 mm per side.
4. This dimension does not include interlead flash. Interlead flash shall not exceed 0.25 mm per side.
5. Reference JEDEC registration MO-153.

A  15
DETAIL A
TYPICAL
SCALE  2.500

## Page 38

www.ti.com
EXAMPLE BOARD LAYOUT
0.05 MAX
ALL AROUND
0.05 MIN
ALL AROUND
24X (1.4)
24X (0.2)
22X (0.4)
(5.9)
(R0.05) TYP
TVSOP - 1.2 mm max heightDGV0024A
SMALL OUTLINE PACKAGE
4229221/A   12/2022
NOTES: (continued)

6. Publication IPC-7351 may have alternate designs.
7. Solder mask tolerances between and around signal pads can vary based on board fabrication site.

LAND PATTERN EXAMPLE
EXPOSED METAL SHOWN
SCALE: 12X
SYMM
SYMM
1
12 13
24
15.000
METAL EDGESOLDER MASK
OPENING
METAL UNDER
SOLDER MASK
SOLDER MASK
OPENING
EXPOSED METALEXPOSED METAL
SOLDER MASK DETAILS
NON-SOLDER MASK
DEFINED
(PREFERRED)
SOLDER MASK
DEFINED

## Page 39

www.ti.com
EXAMPLE STENCIL DESIGN
24X (1.4)
24X (0.2)
22X (0.4)
(5.9)
(R0.05) TYP
TVSOP - 1.2 mm max heightDGV0024A
SMALL OUTLINE PACKAGE
4229221/A   12/2022
NOTES: (continued)

8. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate
    design recommendations.
9. Board assembly site may have different recommendations for stencil design.

SOLDER PASTE EXAMPLE
BASED ON 0.125 mm THICK STENCIL
SCALE: 12X
SYMM
SYMM
1
12 13
24

## Page 40

MECHANICAL DATA
MSSO002E - JANUARY 1995 - REVISED DECEMBER 2001
POST OFFICE BOX 655303 - DALLAS, TEXAS 75265
DB (R-PDSO-G**)   PLASTIC SMALL-OUTLINE
4040065 /E 12/01
28 PINS SHOWN
Gage Plane
8,20
7,40
0,55
0,95
0,25
38
12,90
12,30
28
10,50
24
8,50
Seating Plane
9,907,90
30
10,50
9,90
0,38
5,60
5,00
15
0,22
14
A
28
1
2016
6,506,50
14
0,05 MIN
5,905,90
DIM
A  MAX
A  MIN
PINS **
2,00 MAX
6,90
7,50
0,65 M0,15
0 deg -/C02578 deg
0,10
0,09
0,25
NOTES: A. All linear dimensions are in millimeters.
B. This drawing is subject to change without notice.
C. Body dimensions do not include mold flash or protrusion not to exceed 0,15.
D. Falls within JEDEC MO-150

## Page 41

[No extractable text found on this page.]

## Page 42

www.ti.com
PACKAGE OUTLINE
C
22X 0.65
2X
7.15
24X 0.30
0.19
 TYP6.6
6.2
1.2 MAX
0.15
0.05
0.25
GAGE PLANE
-80
B
NOTE 4
4.5
4.3
A
NOTE 3
7.9
7.7
0.75
0.50
(0.15) TYP
TSSOP - 1.2 mm max heightPW0024A
SMALL OUTLINE PACKAGE
4220208/A   02/2017
1
12
13
24
0.1 C A B
PIN 1 INDEX AREA
SEE DETAIL  A
0.1 C
NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing
    per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. This dimension does not include mold flash, protrusions, or gate burrs. Mold flash, protrusions, or gate burrs shall not
    exceed 0.15 mm per side.
4. This dimension does not include interlead flash. Interlead flash shall not exceed 0.25 mm per side.
5. Reference JEDEC registration MO-153.

SEATING
PLANE
A  20
DETAIL A
TYPICAL
SCALE  2.000

## Page 43

www.ti.com
EXAMPLE BOARD LAYOUT
0.05 MAX
ALL AROUND
0.05 MIN
ALL AROUND
24X (1.5)
24X (0.45)
22X (0.65)
(5.8)
(R0.05) TYP
TSSOP - 1.2 mm max heightPW0024A
SMALL OUTLINE PACKAGE
4220208/A   02/2017
NOTES: (continued)

6. Publication IPC-7351 may have alternate designs.
7. Solder mask tolerances between and around signal pads can vary based on board fabrication site.

LAND PATTERN EXAMPLE
EXPOSED METAL SHOWN
SCALE: 10X
SYMM
SYMM
1
12 13
24
15.000
METALSOLDER MASK
OPENING
METAL UNDER
SOLDER MASK
SOLDER MASK
OPENING
EXPOSED METALEXPOSED METAL
SOLDER MASK DETAILS
NON-SOLDER MASK
DEFINED
(PREFERRED)
SOLDER MASK
DEFINED

## Page 44

www.ti.com
EXAMPLE STENCIL DESIGN
24X (1.5)
24X (0.45)
22X (0.65)
(5.8)
(R0.05) TYP
TSSOP - 1.2 mm max heightPW0024A
SMALL OUTLINE PACKAGE
4220208/A   02/2017
NOTES: (continued)

8. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate
    design recommendations.
9. Board assembly site may have different recommendations for stencil design.

SOLDER PASTE EXAMPLE
BASED ON 0.125 mm THICK STENCIL
SCALE: 10X
SYMM
SYMM
1
12 13
24

## Page 45

GENERIC PACKAGE VIEW
Images above are just a representation of the package family, actual package may vary.
Refer to the product data sheet for package details.
RGE 24 VQFN - 1 mm max height
PLASTIC QUAD FLATPACK - NO LEAD
4204104/H

## Page 46

www.ti.com
PACKAGE OUTLINE
C
SEE TERMINAL
DETAIL
24X 0.3
0.2
2.45 0.1
24X 0.5
0.3
1 MAX
(0.2) TYP
0.05
0.00
20X 0.5
2X
2.5
2X 2.5
A 4.1
3.9
B
4.1
3.9
0.3
0.2
0.5
0.3
VQFN - 1 mm max heightRGE0024B
PLASTIC QUAD FLATPACK - NO LEAD
4219013/A   05/2017
PIN 1 INDEX AREA
0.08 C
SEATING PLANE
1
6 13
18
7 12
24 19
(OPTIONAL)
PIN 1 ID
0.1 C A B
0.05
EXPOSED
THERMAL PAD
25 SYMM
SYMM
NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing
    per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. The package thermal pad must be soldered to the printed circuit board for thermal and mechanical performance.
SCALE  3.000
DETAIL
OPTIONAL TERMINAL
TYPICAL

## Page 47

www.ti.com
EXAMPLE BOARD LAYOUT
0.07 MIN
ALL AROUND
0.07 MAX
ALL AROUND
24X (0.25)
24X (0.6)
( 0.2) TYP
VIA
20X (0.5)
(3.8)
(3.8)
( 2.45)
(R0.05)
TYP
(0.975) TYP
VQFN - 1 mm max heightRGE0024B
PLASTIC QUAD FLATPACK - NO LEAD
4219013/A   05/2017
SYMM
1
6
7 12
13
18
1924
SYMM
LAND PATTERN EXAMPLE
EXPOSED METAL SHOWN
SCALE:15X
NOTES: (continued)

4. This package is designed to be soldered to a thermal pad on the board. For more information, see Texas Instruments literature
    number SLUA271 (www.ti.com/lit/slua271).
5. Vias are optional depending on application, refer to device data sheet. If any vias are implemented, refer to their locations shown
    on this view. It is recommended that vias under paste be filled, plugged or tented.
25
SOLDER MASK
OPENING
METAL UNDER
SOLDER MASK
SOLDER MASK
DEFINED
EXPOSED
METAL
METAL
SOLDER MASK
OPENINGSOLDER MASK DETAILS
NON SOLDER MASK
DEFINED
(PREFERRED)
EXPOSED
METAL

## Page 48

www.ti.com
EXAMPLE STENCIL DESIGN
24X (0.6)
24X (0.25)
20X (0.5)
(3.8)
(3.8)
4X ( 1.08)
(0.64)
TYP
(0.64) TYP
(R0.05) TYP
VQFN - 1 mm max heightRGE0024B
PLASTIC QUAD FLATPACK - NO LEAD
4219013/A   05/2017
NOTES: (continued)

6. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate
   design recommendations.

25
SYMM
METAL
TYP
SOLDER PASTE EXAMPLE
BASED ON 0.125 mm THICK STENCIL

EXPOSED PAD 25
78% PRINTED SOLDER COVERAGE BY AREA UNDER PACKAGE
SCALE:20X
SYMM
1
6
7 12
13
18
1924

## Page 49

IMPORTANT NOTICE AND DISCLAIMER
TI PROVIDES TECHNICAL AND RELIABILITY DATA (INCLUDING DATASHEETS), DESIGN RESOURCES (INCLUDING REFERENCE
DESIGNS), APPLICATION OR OTHER DESIGN ADVICE, WEB TOOLS, SAFETY INFORMATION, AND OTHER RESOURCES "AS IS"
AND WITH ALL FAULTS, AND DISCLAIMS ALL WARRANTIES, EXPRESS AND IMPLIED, INCLUDING WITHOUT LIMITATION ANY
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT OF THIRD
PARTY INTELLECTUAL PROPERTY RIGHTS.
These resources are intended for skilled developers designing with TI products. You are solely responsible for (1) selecting the appropriate
TI products for your application, (2) designing, validating and testing your application, and (3) ensuring your application meets applicable
standards, and any other safety, security, regulatory or other requirements.
These resources are subject to change without notice. TI grants you permission to use these resources only for development of an
application that uses the TI products described in the resource. Other reproduction and display of these resources is prohibited. No license
is granted to any other TI intellectual property right or to any third party intellectual property right. TI disclaims responsibility for, and you fully
indemnify TI and its representatives against any claims, damages, costs, losses, and liabilities arising out of your use of these resources.
TI's products are provided subject to TI's Terms of Sale, TI's General Quality Guidelines, or other applicable terms available either on
ti.com or provided in conjunction with such TI products. TI's provision of these resources does not expand or otherwise alter TI's applicable
warranties or warranty disclaimers for TI products. Unless TI explicitly designates a product as custom or customer-specified, TI products
are standard, catalog, general purpose devices.
TI objects to and rejects any additional or different terms you may propose.
IMPORTANT NOTICE
Copyright (C) 2025, Texas Instruments Incorporated
Last updated 10/2025
