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

#include <assert.h>

#include "modbus.h"

/* PRIVATE, gets the next client info from a master context  or NULL if none left */
static struct _mb_master_clientinfo *_mb_get_next_open_client_connection(mb_master *m)
{

    for (int i = 0; i < MB_MAX_CLIENTS_PER_MASTER; i++)
    {
        if (m->connection_info[i].inuse)
            continue;

        return &m->connection_info[i];
    }

    return NULL;
}

/*
    connects to client, and adds it into the masters info
    returns error OR the newly added clients client idx
*/
mb_error mb_master_add_client_connection(mb_master *m, char *addr, int port, mb_byte client_id)
{
    struct _mb_master_clientinfo *c = _mb_get_next_open_client_connection(m);
    memset(c, 0, sizeof(struct _mb_master_clientinfo));

    if (c == NULL)
    {
        // TODO: Handle better then crashing out
        puts("out of slots for clients!");
        return -1;
    }

    c->fd = socket(AF_INET, SOCK_STREAM, 0);

    if (0 > c->fd)
    {
        // TODO: Handle better then crashing out
        perror("socket");
        return -1;
    }

    inet_pton(AF_INET, addr, &c->addr.sin_addr);
    c->addr.sin_family = AF_INET;
    c->addr.sin_port = htons(port);
    c->addrlen = sizeof(struct sockaddr_in);

    c->header.protocal_identifier = 0;
    c->header.transaction_identifier = 0;
    c->header.unit_identifier = client_id;

    if (0 > connect(c->fd, (struct sockaddr *)&c->addr, c->addrlen))
    {
        // TODO: Handle better then crashing out
        perror("connect");
        return -1;
    }

    puts("Clinet Connected");
    return 0;
}

/* PRIVATE, writes datalen bytes of data into fd

   -1 : error
    0 : ok
*/
static mb_error _mb_master_write(int fd, mb_byte *data, int datalen)
{
    int out = 0;
    do
    {
        int o = write(fd, data + out, datalen - out);
        if (0 > o)
        {
            perror("write");
            fprintf(stderr, "more info out: %d\n", out);
            return -1;
        }

        out += o;
    } while (datalen > out);

    return 0;
}

/* PRIVATE, converts and add i16 to dst */
static void _mb_buffer_add_i16(mb_byte *dst, mb_i16 value)
{
    dst[0] = value >> 8;
    dst[1] = value;
}

static inline mb_i16 _mb_buffer_out_i16(mb_byte *src) { return src[1] | src[0] << 8; }

static mb_ap_header _mb_header_from_buffer(mb_byte *b_in, int len)
{
    mb_byte *b = b_in; /* im not sure that i need to copy this address here... */

    const int headerlen = 7;
    assert(headerlen >= len && "reading a header needs atleast 7 bytes!");

    mb_ap_header r;

    r.transaction_identifier = _mb_buffer_out_i16(b);
    b += sizeof(mb_i16);
    r.protocal_identifier = _mb_buffer_out_i16(b);
    b += sizeof(mb_i16);
    r.length = _mb_buffer_out_i16(b);
    b += sizeof(mb_i16);
    r.unit_identifier = *b;

    return r;
}

/* PRIVATE, reads header bytes off port and assembles them into the header */
static mb_error _mb_read_header(
    int fd,              // file to read from
    mb_ap_header *header // address to write the header to
)
{
    const int headerlen = 7;
    mb_byte headerdata[headerlen];
    memset(headerdata, 0, sizeof(headerdata));

    // read in the MB AP HEADER, then read remainder of data
    int in = 0;
    do
    {
        int r = read(fd, headerdata + in, headerlen - in);
        if (0 > r)
            return -1;
        in += r;
    } while (headerlen > in);

    *header = _mb_header_from_buffer(headerdata, headerlen);

    return 0;
}

/* PRIVATE, reads full modbus message, saves read header into header and message data into buf */
static mb_error _mb_master_read_msg(
    int fd,
    mb_byte *buf,
    int buflen,
    mb_ap_header *header)
{

    int header_read_res = _mb_read_header(fd, header);

    if (0 > header_read_res)
        return header_read_res;

    assert(buflen >= header->length - 1 /*-1 for already read unit address*/
           && "Not enough room to store the remaing data!");

    int in = 0;
    int want = header->length - 1 /*-1 for already read unit address*/;

    do
    {
        int r = read(fd, buf + in, want - in);

        if (0 > r)
            return -1;

        in += r;

    } while (want > in);

    return 0;
}

/* PRIVATE, adds a modbus header into array pointed to by dst */
static void _mb_header(mb_ap_header h, mb_byte *dst, size_t len)
{
    assert(7 < len && "dst len must be larger then a header!");
    // 0, 1 transaction id
    // 2, 3 protocol (0)
    // 4, 5 len
    // 6    unit address

    dst[0] = h.transaction_identifier >> 8;
    dst[1] = h.transaction_identifier;

    dst[2] = h.protocal_identifier >> 8;
    dst[3] = h.protocal_identifier;

    size_t lenmod = len - 6; // length is amount of bytes after this

    dst[5] = lenmod;
    dst[4] = lenmod >> 8;

    dst[6] = h.unit_identifier; // this byte is accounted for in len
}

/* PRIVATE, dumps buffer out into console, kinda formated like a c string, labeld with msg */
static void _mb_dump_buffer(mb_byte *b, int len, const char *msg)
{
    printf("%s", msg);

    printf(": {");

    for (int i = 0; i < len; i++)
    {
        printf("0x%02x, ", b[i]);
    }

    printf("}\n");
}

/* Writes and reads from a connected client designated by client slot, stores values into register array */
mb_error mb_master_read_holding_registers(
    mb_master *m,
    int client_slot,
    mb_i16 register_start,
    mb_i16 quantity_of_registers,
    mb_i16 *out_registers_array,
    int registers_array_len)
{
    //    0     1     2     3     4     5     6     7     8     9     A     B
    // [trans id]  [proto id]  [len come] [UNIT] [RHR] [start addr] [end addr]

    mb_byte write_buffer[12]; //  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    struct _mb_master_clientinfo *mbci = &m->connection_info[client_slot];

    _mb_header(mbci->header, write_buffer, sizeof(write_buffer));

    write_buffer[7] = 0x03; // READ HOLDING REG

    _mb_buffer_add_i16(&write_buffer[8], register_start);         //  8  9
    _mb_buffer_add_i16(&write_buffer[10], quantity_of_registers); // 10 11

#ifdef MB_DEBUG_READ_HOLDING_REG_PRINT_BUFFER
    _mb_dump_buffer(write_buffer, sizeof(write_buffer), "read holding reg tx");
#endif
    if (0 > _mb_master_write(mbci->fd, write_buffer, sizeof(write_buffer)))
    {
        puts("_mb_master_write 0 > wres");
        return -1;
    }

    mb_byte read_buffer[2 + (quantity_of_registers * 2)];
    mb_ap_header read_header;
    memset(&read_header, 0, sizeof(mb_ap_header));

    if (0 > _mb_master_read_msg(mbci->fd, read_buffer, sizeof(read_buffer), &read_header))
    {

        puts("_mb_master_read_msg 0 > rres");
        return -1;
    }

    // at this point in the program, all the data off the port has been read !
#ifdef MB_DEBUG_READ_HOLDING_REG_PRINT_BUFFER
    _mb_dump_buffer(read_buffer, sizeof(read_buffer), "read holding reg rx");
#endif

    mb_byte *ittr = read_buffer + 2 /* + 2 to skip func code and byte count*/;

    for (int i = 0; i < quantity_of_registers; i++)
    {
        out_registers_array[i] = _mb_buffer_out_i16(ittr);
        ittr += sizeof(mb_i16);
    }

    return 0;
}

mb_error mb_master_write_multiple_registers(
    mb_master *m,
    int client,
    mb_i16 first_register,
    mb_i16 *registers,
    int amount)
{
    
}
