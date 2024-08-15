# [MIT 6.S081 Fall 2020] Lab: networking

> https://pdos.csail.mit.edu/6.S081/2020/labs/net.html

```c
/* kernel/e1000.c */

// struct spinlock e1000_lock;
struct spinlock e1000_tx_lock;
struct spinlock e1000_rx_lock;
```

```c
/* kernel/e1000.c */

void e1000_init(uint32 *xregs) {
    int i;

    // initlock(&e1000_lock, "e1000");
    initlock(&e1000_tx_lock, "e1000_tx");
    initlock(&e1000_rx_lock, "e1000_rx");
...
}
```

```c
/* kernel/e1000.c */

int e1000_transmit(struct mbuf *m) {
    //
    // Your code here.
    //
    // the mbuf contains an ethernet frame; program it into
    // the TX descriptor ring so that the e1000 sends it. Stash
    // a pointer so that it can be freed after sending.
    //

    acquire(&e1000_tx_lock);

    int tx_idx = regs[E1000_TDT];
    if (tx_idx > TX_RING_SIZE) {
        return -1;
    }

    struct tx_desc *desc = &tx_ring[tx_idx]; // transmit descriptor
    if (!(desc->status & E1000_RXD_STAT_DD)) {
        return -1; // if previous transmission request is not finished, return error
    }

    tx_mbufs[tx_idx] = m;

    /* fill in descriptor */
    desc->addr = (uint64)m->head;
    desc->length = m->len;
    desc->cmd |= E1000_TXD_CMD_EOP;

    regs[E1000_TDT] = (tx_idx + 1) % TX_RING_SIZE; // increase index by 1

    mbuffree(m); // free used mbuf

    release(&e1000_tx_lock);

    return 0;
}
```

```c
/* kernel/e1000.c */

static void
e1000_recv(void) {
    //
    // Your code here.
    //
    // Check for packets that have arrived from the e1000
    // Create and deliver an mbuf for each packet (using net_rx()).
    //

    acquire(&e1000_rx_lock);

    while (1) {
        int rx_idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE; // increase index by 1
        if (rx_idx > RX_RING_SIZE) {
            goto stop;
        }

        struct rx_desc *desc = &rx_ring[rx_idx]; // recv descriptor
        if (!(desc->status & E1000_RXD_STAT_DD)) {
            goto stop; // if new packet is not available, stop
        }

        struct mbuf *m = rx_mbufs[rx_idx];
        m->len = desc->length;
        net_rx(m);        // deliver mbuf to network stack
        m = mbufalloc(0); // allocate new mbuf

        rx_mbufs[rx_idx] = m;
        desc->addr = (uint64)m->head;
        desc->status = 0;

        regs[E1000_RDT] = rx_idx; // increase index by 1
    }

stop:
    release(&e1000_rx_lock);
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/7595eda3-f28f-49cf-ad21-10437592240e)
