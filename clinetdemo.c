#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>

typedef unsigned char mb_byte;
typedef mb_byte mb_function_code;
typedef unsigned short int mb_i16;

typedef enum mb_functioncode
{
    ReadDiscreteInputs = 2,
    ReadCoils = 1,
    ReadInputRegister = 4,
    ReadHoldingRegisters = 3,
    WriteSingleCoil = 5,
    WriteMultipleCoils = 15,
    WriteSingleRegister = 6,
} mb_functioncode;

typedef enum mb_exceptioncode
{
    IlleagleFunctionCode = 0x01,
    IlleagleDataAddress = 0x02,
    IlleagleDataValue = 0x03,
    ServerFault = 0x04,
    Acknowlage = 0x05,
    ServerBusy = 0x06,
    GatewayProblem = 0x0A,
    GatewayProblem_ = 0x0B,
} mb_exceptioncode;

/* Stuff assigned in this structure must use host to network (hton) fam of methods to convert
the byte order to Big Endian */
typedef struct mb_ap_header // pg: 5/46
{
    mb_i16 transaction_identifier;
    mb_i16 protocal_identifier;
    mb_i16 length;
    mb_byte unit_identifier;
    mb_byte __padding;
} mb_ap_header;

/*   Options that a mb master can have   */
// typedef enum mb_master_options
// {
//     NonBlocking = 1 << 0,
// } mb_master_options;

#define MB_MAX_CLIENTS_PER_MASTER (5)

/* This structure defines the state that our master needs */
typedef struct mb_master
{
    struct _mb_master_clientinfo
    {
        /* Socket data */
        int fd, addrlen;
        struct sockaddr_in addr;

        /* Modbus Data */
        mb_byte client_id;

        /* Housekeeping Data */
        int inuse; // 1 if this structure is in use, 0 if its not
    } connection_info[MB_MAX_CLIENTS_PER_MASTER];
} mb_master;

/* PRIVATE, gets the next client info from a master context  or NULL if none left */
struct _mb_master_clientinfo *_mb_get_next_open_client_connection(mb_master *m)
{

    for (int i = 0; i < MB_MAX_CLIENTS_PER_MASTER; i++)
    {
        if (m->connection_info[i].inuse)
            continue;

        return &m->connection_info[i];
    }
}

/* connects to client, and adds it into the masters info */
void mb_master_add_client_connection(mb_master *m, char *addr, int port, mb_byte client_id)
{
    struct _mb_master_clientinfo *c = _mb_get_next_open_client_connection(m);
    memset(c, 0, sizeof(struct _mb_master_clientinfo));

    if (c == NULL)
    {
        // TODO: Handle better then crashing out
        puts("out of slots for clients!");
        exit(EXIT_FAILURE);
    }

    c->fd = socket(AF_INET, SOCK_STREAM, 0);

    if (0 > c->fd)
    {
        // TODO: Handle better then crashing out
        perror("socket");
        exit(EXIT_FAILURE);
    }

    inet_pton(AF_INET, addr, &c->addr.sin_addr);
    c->addr.sin_family = AF_INET;
    c->addr.sin_port = htons(port);
    c->addrlen = sizeof(struct sockaddr_in);


    if (0 > connect(c->fd, (struct sockaddr *)&c->addr, c->addrlen))
    {
        // TODO: Handle better then crashing out
        perror("connect");
        exit(EXIT_FAILURE);
    }

    puts("Clinet Connected");
}

int main(int argc, char *argv[])
{
    mb_master m;
    memset(&m, 0, sizeof(mb_master));
    mb_master_add_client_connection(&m, "127.0.0.1", 502, 1);

    puts("program done");
}