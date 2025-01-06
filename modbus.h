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

/* This structure defines the state that our server needs, this connects to many clinets */
typedef struct mb_server
{
    struct _mb_clientinfo
    {
        /* Socket data */
        int fd, addrlen;
        struct sockaddr_in addr;

        /* Modbus Data */
        mb_ap_header header;

        /* Housekeeping Data */
        int inuse; // 1 if this structure is in use, 0 if its not
    } connection_info[MB_MAX_CLIENTS_PER_MASTER];
} mb_server;

/*
    connects to client, and adds it into the masters info
    returns error OR the newly added clients client idx
*/
int mb_master_add_client_connection(mb_server *m, char *addr, int port, mb_byte client_id);

/* stores values into register array */
mb_error mb_master_read_holding_registers(
    mb_server *m,
    int client,                   // the value returned by mb_master_add_client_connection
    mb_i16 register_start,        // the register to start reading from
    mb_i16 quantity_of_registers, // how many registers we want to read
    mb_i16 *out_registers_array,  // where to store the resaults
    int registers_array_len       // the length of where to store the resaults
);

/* reads values from registers array */
mb_error mb_master_write_multiple_registers(
    mb_server *m,
    int client,            // the value returned by mb_master_add_client_connection
    mb_i16 first_register, // first register to write to
    mb_i16 *registers,     // pointer to the first register to start writing
    int amount             // the amount of regiseters to write
);

typedef struct Connection
{
    int fd, len;
    struct sockaddr_in saddr;
} Connection;

#ifndef MB_MAX_SERVERS_PER_CLIENT
#define MB_MAX_SERVERS_PER_CLIENT (5)
#endif // MB_MAX_SERVERS_PER_CLIENT

/* this structure represents a modbus server that is connected to us reading data */
typedef struct _mb_serverinfo
{
    /* Socket data */
    int fd, addrlen;
    struct sockaddr_in addr;

    /* Modbus Data */
    mb_ap_header header;

    /* Housekeeping Data */
    int inuse; // 1 if this structure is in use, 0 if its not
} _mb_serverinfo;

/* a mb_server serves up its array of registers to many clients */
typedef struct mb_client
{
    _mb_serverinfo connection_info[MB_MAX_SERVERS_PER_CLIENT];

    /* Modbus Data */
    mb_i16 nodeid;      // this clients node id
    mb_i16 *registers;  // ptr to user registers
    int register_count; // length of registers we provide

    /* that is listening server socket */

    Connection svr;
} mb_client;
mb_error mb_svr_init(mb_client *s, int16_t *registers, mb_i16 registersCount, int port, mb_i16 nodeId);
mb_error mb_svr_process_clients(mb_client *s);
mb_error mb_svr_accept_new_clients(mb_client *s);
#endif // MODBUS_H