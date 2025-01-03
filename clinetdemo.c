#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "modbus.h"

int main(int argc, char *argv[])
{
    mb_master m;
    memset(&m, 0, sizeof(mb_master));
    int clientIdx;
    if (0 > (clientIdx = mb_master_add_client_connection(&m, "172.19.144.1", 502, 1)))
    {
        printf("Failed to add clinet!\n");
        exit(EXIT_FAILURE);
    }

    while (1)
    {

        mb_i16 reg[2];

        if (0 > mb_master_read_holding_registers(&m, clientIdx, 1, 2, reg, sizeof(reg) / sizeof(reg[0])))
        {
            puts("error reading");
            break;
        }

        printf("%d, %d\n", reg[0], reg[1]);

        reg[0] = 5050;
        reg[1] = 686;

        if (0 > mb_master_write_multiple_registers(&m, clientIdx, 1, reg, sizeof(reg) / sizeof(reg[0])))
        {
            puts("error writing");
            break;
        }

        sleep(1);
    }
}