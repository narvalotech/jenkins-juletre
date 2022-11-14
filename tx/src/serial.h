#ifndef SERIAL_H_
#define SERIAL_H_

struct rx_uart_header {
    char start[4];		/* spells U A R T */
    uint16_t len;
    uint8_t crc;		/* CRC of whole frame */
    uint8_t idx;		/* Current index, used when building header */
};

struct rx_uart {
    const struct device *uart;

    struct rx_uart_header *header;

    /* ring buffer: stores all received uart data */
    struct ring_buf *ringbuf;

    /* packet buffer: stores only the packet to be sent out */
    char *packet;
    uint16_t packet_max_len;
};

int serial_init(struct rx_uart* uart_config);

#endif // SERIAL_H_
