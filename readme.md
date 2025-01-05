
# THIS IS A WORK IN PROGRESS
- TODO yet
- [X] Read Holding Registers  (3)
- [X] Write Multiple Registers (16)
- [] Read Discrete Inputs     (2)
- [] Read Coils
- [] Write Single Coil
- [] Write Multiple Coils
- [] Write Single Register

- [] Read and write error function code handling


# Requrements
- Needs a version of C that supports Varable Length Arrays
    - I dont think this is a hard requirement, there is only a few places where a VLA could be replaced with some kinda static size. There is a set max amount of bytes that a modbus message could give, thus making it possable to remove this VLA pretty easy.


# Using WSL 
- Im using the program Modbus Tester by Graham Ross running in windows.
- To run the client demo, your windows computer is going to be our slave, so we need to run `ip route show | grep -i default | awk '{ print $3}'` and use that ip to connect to, for more info microsoft has some info here. [wsl networking](https://learn.microsoft.com/en-us/windows/wsl/networking#accessing-windows-networking-apps-from-linux-host-ip)