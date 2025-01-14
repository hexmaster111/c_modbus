
# C Modbus


# supported modbus function codes
- Read Holding Registers  (3)
- Write Multiple Registers (16)

# Unsupported fucntion codes
- Read Discrete Inputs 
- Read Coils
- Write Single Coil
- Write Multiple Coils
- Write Single Register

# Requrements
- Needs a version of C that supports Varable Length Arrays
    - I dont think this is a hard requirement, there is only a few places where a VLA could be replaced with some kinda static size. There is a set max amount of bytes that a modbus message could give, thus making it possable to remove this VLA pretty easy.


# Server Demo (Your code will be providing registers for other things to get)

```c

mb_i16 g_registers[10]; // the registers that the server will be passing out


mb_client svr = {0};
mb_svr_init(&svr, g_registers, 10, 5003, 1); // Setup the server, Tell it how many registers and what port to work with


while (1) // Server Loop
{
    mb_svr_accept_new_clients(&svr);
    mb_svr_process_clients(&svr);
    /* Do whatever other work you want */
}

mb_svr_deinit(&svr); // cleanup
```

# Client Demo (Your code will be getting / setting register on a server somewhere)

``` c

mb_server m = {0}; // the context for your modbus connection info
int clinet; // the client id for reading / writing to it

mb_i16 reg[2] = {0, 0}; // A place for us to read / write registers into from the network

// Connect to our server
if (0 > (client = mb_master_add_client_connection(&m, "192.168.1.6", 502, 1)))
{
    printf("Failed to add clinet!\n");
    exit(EXIT_FAILURE);
}


// read a 2 registers starting at address 1 into our reg array
if (0 > mb_master_read_holding_registers(&m, client, 1, 2, reg, sizeof(reg) / sizeof(reg[0])))
{
    puts("error reading");
    exit(EXIT_FAILURE);
}

// Do Something with the values!


// writing values into the client from our registers array
if (0 > mb_master_write_multiple_registers(&m, client, 1, reg, sizeof(reg) / sizeof(reg[0])))
{
    puts("error writing");
    exit(EXIT_FAILURE);
}
```



# Using WSL 
- Im using the program Modbus Tester by Graham Ross running in windows.
- To run the client demo, your windows computer is going to be our slave, so we need to run `ip route show | grep -i default | awk '{ print $3}'` and use that ip to connect to, for more info microsoft has some info here. [wsl networking](https://learn.microsoft.com/en-us/windows/wsl/networking#accessing-windows-networking-apps-from-linux-host-ip)