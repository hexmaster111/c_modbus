#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include "modbus.h"

#define REGISTER_COUNT (100)
mb_i16 g_registers[REGISTER_COUNT];
int g_reghash;

int modified_bernstein(int16_t key)
{
    unsigned ret = key & 0xffffffff;
    ret = 33 * ret ^ (key >> 8);
    return ret % __INT16_MAX__; // Try to do some modulo math to keep it in range
}

int main(int argc, char *argv[])
{
 
    for (int i = 0; i < REGISTER_COUNT; i++)
    {
        g_registers[i] = i + 1;
    }

    mb_client svr = {0};
    mb_svr_init(&svr, g_registers, REGISTER_COUNT, 5003, 1);
    printf("Server Listing\n");

    while (1)
    {
        // puts("start loop");
        mb_svr_accept_new_clients(&svr);
        mb_svr_process_clients(&svr);
 
        /*          do some other work
        if this is your program, you may wanna use the
           values that you get some how~, we are just gonan
           print them out for simplicty
         */
        int hashnow = 0;
        for (int i = 0; i < REGISTER_COUNT; i++)
        {
            hashnow += modified_bernstein(g_registers[i]);
        }

        if (hashnow != g_reghash)
        {

            for (int i = 0; i < REGISTER_COUNT; i++)
            {
                if (i % 8 == 0)
                    printf("\n");

                printf("%4d\t", g_registers[i]);
            }
            printf("\n");

            g_reghash = hashnow;
        }
        // sleep(1);
    }
}