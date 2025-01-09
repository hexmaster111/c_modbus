#define _GNU_SOURCE
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
#include <errno.h>

#include <assert.h>

#include "modbus.h"

mb_error _mb_svr_write_illeagle_address(mb_client *sv, struct _mb_serverinfo *s);

/* PRIVATE, gets the next client info from a master context  or NULL if none left */
static struct _mb_clientinfo *_mb_get_next_open_client_connection(mb_server *m)
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
int mb_master_add_client_connection(mb_server *m, char *addr, int port, mb_i8 client_id)
{
    struct _mb_clientinfo *c = _mb_get_next_open_client_connection(m);
    memset(c, 0, sizeof(struct _mb_clientinfo));

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
static mb_error _mb_master_write(int fd, mb_i8 *data, int datalen)
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
static void _mb_buffer_add_i16(mb_i8 *dst, mb_i16 value)
{
    dst[0] = value >> 8;
    dst[1] = value;
}

static inline mb_i16 _mb_buffer_out_i16(mb_i8 *src) { return src[1] | src[0] << 8; }

static mb_ap_header _mb_header_from_buffer(mb_i8 *b_in, int len)
{
    mb_i8 *b = b_in; /* im not sure that i need to copy this address here... */

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
    mb_i8 headerdata[headerlen];
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
    mb_i8 *buf,
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
static void _mb_header(mb_ap_header h, mb_i8 *dst, size_t len)
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
static void _mb_dump_buffer(mb_i8 *b, int len, const char *msg)
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
    mb_server *m,
    int client_slot,
    mb_i16 register_start,
    mb_i16 quantity_of_registers,
    mb_i16 *out_registers_array,
    int registers_array_len)
{
    //    0     1     2     3     4     5     6     7     8     9     A     B
    // [trans id]  [proto id]  [len come] [UNIT] [RHR] [start addr] [end addr]

    mb_i8 write_buffer[12]; //  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    struct _mb_clientinfo *mbci = &m->connection_info[client_slot];

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

    mb_i8 read_buffer[2 + (quantity_of_registers * 2)];
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

    mb_i8 *ittr = read_buffer + 2 /* + 2 to skip func code and byte count*/;

    for (int i = 0; i < quantity_of_registers; i++)
    {
        out_registers_array[i] = _mb_buffer_out_i16(ittr);
        ittr += sizeof(mb_i16);
    }

    return 0;
}

mb_error mb_master_write_multiple_registers(
    mb_server *m,
    int client,
    mb_i16 first_register,
    mb_i16 *registers,
    int register_count)
{
    int wblen = 7 + 1 + sizeof(mb_i16) + sizeof(mb_i16) + sizeof(mb_i8) + (register_count * sizeof(mb_i16));
    // header + opcode + first reg addr + reg count + bytecount |=> + (register_count * sizeof(i16))
    mb_i8 write_buffer[wblen];

    memset(write_buffer, 0xFF, sizeof(write_buffer));
    struct _mb_clientinfo *ci = &m->connection_info[client];
    _mb_header(ci->header, write_buffer, sizeof(write_buffer));

    write_buffer[7] = 0x10; /* Write Reg func code */

    _mb_buffer_add_i16(&write_buffer[8], first_register);  //  8  9
    _mb_buffer_add_i16(&write_buffer[10], register_count); // 10 11
    write_buffer[12] = register_count * sizeof(mb_i16);    /* bytes of data to come */

    for (int i = 0; i < register_count; i++)
    {
        int loc = 13 + (i * sizeof(mb_i16));
        _mb_buffer_add_i16(&write_buffer[loc], registers[i]);
    }

#ifdef DEBUG_PRINT_TX_BUFFER
    _mb_dump_buffer(write_buffer, sizeof(write_buffer), "write multi registers tx");
#endif // DEBUG_PRINT_TX_BUFFER

    if (0 > _mb_master_write(ci->fd, write_buffer, sizeof(write_buffer)))
    {
        return -1;
    }

    // at this point we have written all the data, now we need to read back and make sure it wrote correctly

    mb_i8 read_buffer[7 + 5]; // header + responce data
    mb_ap_header read_header;
    memset(&read_header, 0, sizeof(mb_ap_header));

    if (0 > _mb_master_read_msg(ci->fd, read_buffer, sizeof(read_buffer), &read_header))
    {

        puts("_mb_master_read_msg 0 > rres");
        return -1;
    }

    // could be an exception code OR responce

    // TODO: if (returned func code == 9) { return error, perhaps a -2 to indcate an exception, or change return type? }

#ifdef DEBUG_PRINT_RX_BUFFER
    _mb_dump_buffer(read_buffer, sizeof(read_buffer), "write responce RX");
#endif // DEBUG_PRINT_RX_BUFFER
    ci->header.transaction_identifier += 1;

    return 0;
}

Connection create_tcp_socket_fd(int port)
{
    int server_fd, len;
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (0 > server_fd)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (0 > setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

#ifndef SO_REUSEPORT
#define SO_REUSEPORT (15) /*my ide is silly or i am idk*/
#endif

    if (0 > setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (0 > bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (0 > listen(server_fd, 3))
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return (struct Connection){.fd = server_fd, .len = len, .saddr = server_addr};
}

struct _mb_serverinfo *_mb_svr_get_next_server_info(mb_client *s)
{
    for (int i = 0; i < MB_MAX_SERVERS_PER_CLIENT; i++)
    {
        if (!s->connection_info[i].inuse)
            return &s->connection_info[i];
    }
    return NULL;
}

mb_error mb_svr_init(mb_client *s, int16_t *registers, mb_i16 registersCount, int port, mb_i16 nodeId)
{
    memset(s, 0, sizeof(mb_client));
    s->nodeid = nodeId;
    s->svr = create_tcp_socket_fd(port);
    s->registers = registers;
    s->register_count = registersCount;
}

mb_error _mb_svr_read_i16(struct _mb_serverinfo *s, mb_i16 *out)
{
    mb_i8 rd[2];

    int in = 0;
    do
    {

        int r = read(s->fd, &rd, 2);
        if (0 > r)
        {
            perror("read");
            return -1;
        }
        in += r;

    } while (2 > in);

    *out = rd[1] | rd[0] << 8;

    return 0;
}

mb_error _mb_svr_do_read_holding_registers(mb_client *sv, struct _mb_serverinfo *s)
{
    // read holding registers params in stream   [i16 - first address] [i16 - count]

    mb_i16 first_addr, reg_count;

    if (0 > _mb_svr_read_i16(s, &first_addr))
        return -1;
    if (0 > _mb_svr_read_i16(s, &reg_count))
        return -1;

    if (first_addr + reg_count > sv->register_count)
    {
        if (0 > _mb_svr_write_illeagle_address(sv, s))
        {
            return -1;
        }
    }

    // [header 7] [func code 1] [byte count 1] [data....]
    mb_i8 outbuffer[7 + 1 + 1 + reg_count * sizeof(mb_i16)];
    memset(outbuffer, 0, sizeof(outbuffer));

    _mb_header(s->header, outbuffer, sizeof(outbuffer));

    outbuffer[7] = 0x03;
    outbuffer[8] = reg_count * sizeof(mb_i16);

    for (int i = 0; i < reg_count; i++)
    {
        _mb_buffer_add_i16(&outbuffer[(i * 2) + 9], sv->registers[first_addr + i]);
    }

#ifdef MB_DEBUG_DO_READ_HOLDING_REG_DUMP_BUFFER
    _mb_dump_buffer(outbuffer, sizeof(outbuffer), "DO READ HOLDING REG TX");
#endif

    if (0 > _mb_master_write(s->fd, outbuffer, sizeof(outbuffer)))
    {
        return -1;
    }
}

mb_error _mb_svr_write_exception_code(int fd, mb_i8 except, mb_ap_header header)
{

    mb_i8 buff[7 + 1 + 1]; // header + func + exception code
    memset(buff, 0, sizeof(buff));
    _mb_header(header, buff, sizeof(buff));

    int to_write = sizeof(buff);

    do
    {
        int w = write(fd, buff, to_write);
        if (0 > w)
            return -1;
        to_write -= w;
    } while (to_write > 0);

    return 0;
}

mb_error _mb_svr_empty_stream(mb_client *sv, struct _mb_serverinfo *s)
{
    /* there should be 2 less then the headers len bytes left to read */
    int want = s->header.length - 2;
    char trash[want];
    int in = 0;

    do
    {
        int r = read(s->fd, trash, want - in);
        if (0 > r)
        {
            perror("read");
            return -1;
        }

        in += r;

    } while (want > in);

    return 0;
}

mb_error _mb_svr_write_illeagle_address(mb_client *sv, struct _mb_serverinfo *s)
{
    puts("ILLEAGLE ADDRESS OR REQUEST TOO LARGE");
    if (0 > _mb_svr_empty_stream(sv, s))
        return -1;

    if (0 > _mb_svr_write_exception_code(s->fd, 0x02, s->header))
        return -1;
}

mb_error _mb_svr_write_function_code_not_supported(mb_client *sv, struct _mb_serverinfo *s)
{

    if (0 > _mb_svr_empty_stream(sv, s))
        return -1;

    if (0 > _mb_svr_write_exception_code(s->fd, 0x01, s->header))
        return -1;
}

/* this method sets the servers register values */
void _mb_svr_set_registers(
    mb_client *sv,
    struct _mb_serverinfo *s,
    mb_i16 first_address,
    mb_i16 register_count,
    mb_i8 *values,
    int valueslen)
{
    for (int i = 0; i < register_count; i++)
    {
        sv->registers[i + first_address] = _mb_buffer_out_i16(values + (i * sizeof(mb_i16)));
    }
}

mb_error _mb_svr_do_write_holding_registers(mb_client *sv, struct _mb_serverinfo *s)
{
    // [i16 first reg addr] [i16 register count] [i8 byte count |=> ] [... data ...]

    mb_i16 first_reg_addr, reg_count;
    mb_i8 remainder;

    int want = 5;
    mb_i8 buf[want];

    do
    {
        int r = read(s->fd, buf, want);
        if (0 > r)
        {
            perror("read");
            return -1;
        }
        want -= r;
    } while (want > 0);

    first_reg_addr = _mb_buffer_out_i16(buf);
    reg_count = _mb_buffer_out_i16(buf + sizeof(mb_i16));
    remainder = *(buf + sizeof(mb_i16) + sizeof(mb_i16));
    int payload_size = remainder;
    mb_i8 payload_data[payload_size];

    memset(payload_data, 0, payload_size);

    do
    {
        int r = read(s->fd, payload_data, remainder);
        if (0 > r)
        {
            perror("read");
            return -1;
        }
        remainder -= r;
    } while (remainder > 0);

    printf("Got %d bytes\n", payload_size);

#ifdef _mb_dump_buffer_payload_data_DEBUG_PRINT
    _mb_dump_buffer(payload_data, payload_size, "Write Data Bytes");
#endif


    /* at this point, we have the data, we should have handled it by now, and we need to send a responce back to the server */
    _mb_svr_set_registers(sv, s, first_reg_addr, reg_count, payload_data, payload_size);

    /*header + func code + start + count*/
    int responce_size = 7 + sizeof(mb_i8) + sizeof(mb_i16) + sizeof(mb_i16);
    mb_i8 outbuff[responce_size];
    memset(outbuff, 0xFF, sizeof(outbuff));

    _mb_header(s->header, outbuff, sizeof(outbuff));

    outbuff[7] = 16; /* function code, ~should~ be set by reading in the write methods */

    _mb_buffer_add_i16(outbuff + 7 + 1, first_reg_addr);
    _mb_buffer_add_i16(outbuff + 7 + 1 + sizeof(mb_i16), reg_count);

    int out = responce_size;

    do
    {
        int o = write(s->fd, outbuff, out);
        if (0 > o)
        {
            perror("write");
            return -1;
        }
        out -= o;
    } while (out > 0);

    return 0;
}

mb_error _mb_svr_read_and_do_command(mb_client *sv, struct _mb_serverinfo *s)
{

    char buff[7] = {0};
    const int want = 7;
    int in = 0;
    do
    {
        int n = read(s->fd, &buff, sizeof(buff));
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return 0; /* nothing to read */
        }

        if (n == 0 || 0 > n)
        {

            perror("read");
            return -1;
        }

        in += n;
    } while (want > in);
    s->header = _mb_header_from_buffer(buff, 7);
    /* the next bytes should be the func code */

    uint8_t funccode;

    if (0 > read(s->fd, &funccode, 1))
    {
        perror("read");
        return -1;
    }

    mb_error handle_err;

    switch (funccode)
    {
    case 3:
        handle_err = _mb_svr_do_read_holding_registers(sv, s);
        break;
    case 16:
        handle_err = _mb_svr_do_write_holding_registers(sv, s);
        break;

    default:
        printf("UNSUPPORTED FN CODE %d\n", funccode);
        handle_err = _mb_svr_write_function_code_not_supported(sv, s);
    }

    if (0 > handle_err)
    {
        return -1;
    }

    // at this point, all the data is out of the socket
    return 0;
}

mb_error mb_svr_process_clients(mb_client *s)
{
    for (int i = 0; i < MB_MAX_SERVERS_PER_CLIENT; i++)
    {
        if (!s->connection_info[i].inuse)
            continue;

        int rc;
        if (0 > (rc = _mb_svr_read_and_do_command(s, &s->connection_info[i])))
        {
            /*todo: disconnect client and remove */
        }
    }
}

mb_error mb_svr_accept_new_clients(mb_client *s)
{
    struct sockaddr_in connection_addr;
    int connection_addr_len = sizeof(connection_addr);

    memset(&connection_addr, 0, sizeof(connection_addr));

    int connection_fd = accept4(s->svr.fd, (struct sockaddr *)&connection_addr, &connection_addr_len, SOCK_NONBLOCK);
    if (0 > connection_fd)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            return 0; // no work
        }

        perror("accept");
        return -1;
    }
    printf("accepting client fd: %d\n", connection_fd);

    struct _mb_serverinfo *sslot = _mb_svr_get_next_server_info(s);
    sslot->addr = connection_addr;
    sslot->addrlen = connection_addr_len;
    sslot->fd = connection_fd;
    sslot->inuse = 1;
    puts("connection accepted");
}