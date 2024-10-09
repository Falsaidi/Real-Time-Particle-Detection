# Overview
This repository stores all the code necessary for the real-time particle detection system. Simulating the full flow requires a Raspberry Pi, a laptop or computer, and an Arduino, with Python installed on the Pi and computer.
# Usage
Install [simulate.py](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/simulate.py) onto a Raspberry Pi connected to a computer via ethernet. The Pi must be assigned an IP address of `10.10.1.10` with subnet mask `255.255.255.0`. A binary file `data.bin` must be located in the same directory which contains the raw data as packets of 1036 bytes without any header information. This can be done by cutting out the header in a hex editor from any raw data file.

Once connected, install [start.py](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/start.py) and [program.exe](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/program.exe) onto the computer. `program.exe` is the compiled file of [ParticleDetect.cpp](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/ParticleDetect.cpp) and can be compiled manually using the following g++ command:
```bash
g++ ParticleDetect.cpp -I"directory\to\boost\library" -lws2_32 -o program
```
> Note: [Boost](https://www.boost.org/) must be installed and referenced in the command to successfully compile the code.

`start.py` will call `program.exe` after initializing a binary file with header information and clearing all other output files. The electrode locations can be defined within `start.py` by modifiying the variable `ELECTRODE_LOCATIONS` to specify the center of each electrode. `program.exe` will begin to communicate with the Pi, opening a socket to allow for information to be received. 

This data will be processed in real-time, detecting any particles passing through the machine. When a particle is detected, a single byte indicating the electrode that the particle is in line with will be sent through serial port `COM6` (default 9600 baud rate) to communicate with the Arduino.

The file [ElectrodeActivate.ino](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/ElectrodeActivate/ElectrodeActivate.ino) must be uploaded onto an Arduino connected to COM6 on the computer. This program will receive the corresponding byte data from `program.exe` and send a GPIO signal to activate the corresponding electrode.

Connecting these programs to the DEP Impedance Cytometer only requires the Raspberry Pi to be replaced with the machine, everything should work seamlessly otherwise.
## Real Time Programs
### [simulate.py](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/simulate.py)
The only program running on the Raspberry Pi. It simulates the machine and outputs all the data in 1036 byte packets with a hard-coded delay (default 5ms). It reads from a binary file `data.bin` which does not include any header information, only the packets. [Putty](https://www.chiark.greenend.org.uk/~sgtatham/putty/) or [WinSCP](https://winscp.net/eng/index.php) can be used to transfer the data to the Raspberry Pi, or installed directly from Github using a monitor.
### [start.py](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/start.py)
Calls the C++ program and initializes all necessary files. The beginning of the program contains variables which can be edited to enable/disable functionality of the program:
- `CPP_RUN` (True/False): Indicates whether `program.exe` should be called and run
- `PROCESS_GRAPH` (True/False): Indicates whether graphs should be made for each electrode snippet
    - Requires `WRITE_SNIPPETS` to be `1`
- `GRAPH_PARTICLES` (True/False): Indicates whether every particle should be graphed when detected
	- Requires `WRITE_PARTICLES` to have been on and written to `particles.csv`
- `WRITE_BIN` (0/1): Indicates whether the raw binary data should be written to a file when read
    - Initializes the binary file (default `data.bin`) with header data
- `WRITE_SNIPPETS` (0/1): Indicates whether snippet data should be written to a file
- `WRITE_REMEDIANS` (0/1): Indicates whether remedian values should be written to a file
- `WRITE_PARTICLES` (0/1): Indicates whether particles should be written to a file when detected

The variable `ELECTRODE_LOCATIONS` stores the centers of each electrode as pixel values.
### [ParticleDetect.cpp](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/ParticleDetect.cpp)
Processes all incoming binary data and detects any particles in real-time. The program flow is as follows:
- Receive binary data as a segment (4 segments per line)
- Extract any snippets within the segment
- For each snippet, asynchronously subtract the background by calculating the [remedian](https://www.researchgate.net/publication/247974442_The_Remedian_A_Robust_Averaging_Method_for_Large_Data_Sets)
- Calculate the 2D average of each snippet and activate the corresponding electrode if it passes a threshold

The only external library used is [Boost](https://www.boost.org/).

The program has 5 parameters:
- The path to store the raw data (if `WRITE_BIN` is set to 1)
- `WRITE_BIN`
- `WRITE_SNIPPETS`
- `WRITE_REMEDIANS`
- `ELECTRODE_LOCATIONS`
> Note: These arguments can be set in the Python program by changing the corresponding variables. No arguments when running the program will set default values.
### [ElectrodeActivate.ino](https://github.com/Biosensors-Research-Lab/Lensless-DEP-Impedance-Cytometer---Real-Time-Particle-Detection/blob/main/ElectrodeActivate.ino)
Arduino code which receives Serial input and activates a GPIO port corresponding to an electrode. The Arduino must be connected to COM6 and with a baud rate of 9600.