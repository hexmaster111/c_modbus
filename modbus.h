#ifndef MODBUS_H
#define MODBUS_H

#include <netinet/in.h>

typedef unsigned char mb_byte;
typedef unsigned short int mb_i16;

/*
    this type shows that the function can fail, error if  0 > mb_error
*/
typedef int mb_error;

/* Stuff assigned in this structure must use host to network (hton) fam of methods to convert
the byte order to Big Endian */
typedef struct mb_ap_header // pg: 5/46
{
    mb_i16 transaction_identifier;
    mb_i16 protocal_identifier;
    mb_i16 length;
    mb_byte unit_identifier;
} mb_ap_header;

/*   Options that a mb master can have   */
// typedef enum mb_master_options
// {
//     NonBlocking = 1 << 0,
// } mb_master_options;

#ifndef MB_MAX_CLIENTS_PER_MASTER
#define MB_MAX_CLIENTS_PER_MASTER (5)
#endif

/* This structure defines the state that our master needs */
typedef struct mb_master
{
    struct _mb_master_clientinfo
    {
        /* Socket data */
        int fd, addrlen;
        struct sockaddr_in addr;

        /* Modbus Data */
        mb_ap_header header;

        /* Housekeeping Data */
        int inuse; // 1 if this structure is in use, 0 if its not
    } connection_info[MB_MAX_CLIENTS_PER_MASTER];
} mb_master;

/*
    connects to client, and adds it into the masters info
    returns error OR the newly added clients client idx
*/
mb_error mb_master_add_client_connection(mb_master *m, char *addr, int port, mb_byte client_id);

/* stores values into register array */
mb_error mb_master_read_holding_registers(
    mb_master *m,
    int client,                   // the value returned by mb_master_add_client_connection
    mb_i16 register_start,        // the register to start reading from
    mb_i16 quantity_of_registers, // how many registers we want to read
    mb_i16 *out_registers_array,  // where to store the resaults
    int registers_array_len       // the length of where to store the resaults
);

/* reads values from registers array */
mb_error mb_master_write_multiple_registers(
    mb_master *m,
    int client,            // the value returned by mb_master_add_client_connection
    mb_i16 first_register, // first register to write to
    mb_i16 *registers,     // pointer to the first register to start writing
    int amount             // the amount of regiseters to write
);

#endif // MODBUS_H