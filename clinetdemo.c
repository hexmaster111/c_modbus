#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "modbus.h"

int main(int argc, char *argv[])
{
    mb_server m;
    memset(&m, 0, sizeof(mb_server));
    int client;
    puts("Connecting client");
    if (0 > (client = mb_master_add_client_connection(&m, "192.168.1.6", 502, 1)))
    {
        printf("Failed to add clinet!\n");
        exit(EXIT_FAILURE);
    }

    mb_i16 reg[2] = {0, 0};

    while (1)
    {

        if (0 > mb_master_read_holding_registers(&m, client, 1, 2, reg, sizeof(reg) / sizeof(reg[0])))
        {
            puts("error reading");
            break;
        }

        printf("%d, %d\n", reg[0], reg[1]);
        reg[0] += 1;

        if (0 > mb_master_write_multiple_registers(&m, client, 1, reg, sizeof(reg) / sizeof(reg[0])))
        {
            puts("error writing");
            break;
        }

        sleep(5);
    }
}