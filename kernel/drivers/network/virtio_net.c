/**
 * @file virtio_net.c
 * @brief Virtio-net-pci legacy NIC driver for SecureOS.
 *
 * Purpose:
 *   Implements a polling-based virtio-net (legacy, PCI) driver targeted at
 *   QEMU and compatible virtualizers.  Uses the standard PCI configuration
 *   space (ports 0xCF8/0xCFC) to find device 0x1AF4:0x1000, reads BAR0
 *   for the I/O base, configures guest/host features, maps static virtqueue
 *   descriptors, and provides send/receive via polling the used rings.
 *
 * Interactions:
 *   - network_hal.c: driver calls network_hal_register_primary() on success.
 *   - kmain.c: virtio_net_init_primary() is called during boot.
 *   - eth.c, ipv4.c: send/receive raw Ethernet frames.
 *
 * Limitations (v1):
 *   - Polling only; no interrupt handling.
 *   - Single TX and RX queue.
 *   - Static 16-slot descriptor rings at compile-time addresses.
 *   - Guest IP address is hardcoded (no DHCP).
 *
 * Launched by:
 *   Called from kmain.c during kernel boot.  Not a standalone process;
 *   compiled into the kernel image.
 */

#include "virtio_net.h"

#include <stdint.h>
#include <stddef.h>

#include "../../hal/network_hal.h"

/* -----------------------------------------------------------------------
 * x86 I/O port helpers (same convention as serial.c / ata_pio.c)
 * --------------------------------------------------------------------- */

static inline void vnet_outb(unsigned short port, unsigned char val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void vnet_outw(unsigned short port, unsigned short val) {
  __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void vnet_outl(unsigned short port, unsigned int val) {
  __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char vnet_inb(unsigned short port) {
  unsigned char ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline unsigned short vnet_inw(unsigned short port) {
  unsigned short ret;
  __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline unsigned int vnet_inl(unsigned short port) {
  unsigned int ret;
  __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

/* -----------------------------------------------------------------------
 * PCI configuration space access (CF8/CFC mechanism)
 * --------------------------------------------------------------------- */

#define PCI_CONFIG_ADDR 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static unsigned int pci_cfg_read32(unsigned int bus, unsigned int dev,
                                   unsigned int func, unsigned int reg) {
  unsigned int addr = (1u << 31u) |
                      ((bus  & 0xFFu) << 16u) |
                      ((dev  & 0x1Fu) << 11u) |
                      ((func & 0x07u) <<  8u) |
                      (reg & 0xFCu);
  vnet_outl((unsigned short)PCI_CONFIG_ADDR, addr);
  return vnet_inl((unsigned short)PCI_CONFIG_DATA);
}

static void pci_cfg_write32(unsigned int bus, unsigned int dev,
                             unsigned int func, unsigned int reg,
                             unsigned int val) {
  unsigned int addr = (1u << 31u) |
                      ((bus  & 0xFFu) << 16u) |
                      ((dev  & 0x1Fu) << 11u) |
                      ((func & 0x07u) <<  8u) |
                      (reg & 0xFCu);
  vnet_outl((unsigned short)PCI_CONFIG_ADDR, addr);
  vnet_outl((unsigned short)PCI_CONFIG_DATA, val);
}

/* Enable bus-master and I/O space access in PCI command register. */
static void pci_enable_device(unsigned int bus, unsigned int dev, unsigned int func) {
  unsigned int cmd_status = pci_cfg_read32(bus, dev, func, 0x04u);
  /* Bit 0 = I/O space enable, Bit 2 = bus master enable */
  cmd_status |= 0x05u;
  pci_cfg_write32(bus, dev, func, 0x04u, cmd_status);
}

/* -----------------------------------------------------------------------
 * Virtio legacy I/O register offsets (relative to BAR0 I/O base)
 * See: https://docs.oasis-open.org/virtio/virtio/v1.0/cs04/
 *       Appendix B "Virtio Over PCI Bus" – legacy interface
 * --------------------------------------------------------------------- */

enum {
  VIRTIO_PCI_DEVICE_FEATURES = 0x00,  /* 32-bit RO: host feature bits       */
  VIRTIO_PCI_GUEST_FEATURES  = 0x04,  /* 32-bit WO: negotiated features      */
  VIRTIO_PCI_QUEUE_ADDR      = 0x08,  /* 32-bit WO: ring physical addr >> 12 */
  VIRTIO_PCI_QUEUE_SIZE      = 0x0C,  /* 16-bit RO: number of descriptors    */
  VIRTIO_PCI_QUEUE_SELECT    = 0x0E,  /* 16-bit WO: select which queue       */
  VIRTIO_PCI_QUEUE_NOTIFY    = 0x10,  /* 16-bit WO: kick queue N             */
  VIRTIO_PCI_STATUS          = 0x12,  /* 8-bit RW: device status flags       */
  VIRTIO_PCI_ISR             = 0x13,  /* 8-bit RO: ISR status (clear on read)*/
  VIRTIO_PCI_CONFIG          = 0x14,  /* net config starts here              */

  /* Device status bits */
  VIRTIO_STATUS_ACK           = 0x01,
  VIRTIO_STATUS_DRIVER        = 0x02,
  VIRTIO_STATUS_DRIVER_OK     = 0x04,
  VIRTIO_STATUS_FAILED        = 0x80,

  /* Feature bits for virtio-net */
  VIRTIO_NET_F_MAC            = (1u << 5u),

  /* Queue indices */
  VIRTIO_NET_QUEUE_RX         = 0,
  VIRTIO_NET_QUEUE_TX         = 1,
};

/* -----------------------------------------------------------------------
 * Virtqueue layout (split-ring, legacy alignment rules)
 * Descriptor: 16 bytes; Available ring entry: 2 bytes; Used ring entry: 8 bytes
 * All three parts placed in a contiguous aligned 4096-byte page.
 * --------------------------------------------------------------------- */

enum {
  VRING_SIZE     = 256,  /* must match QEMU virtio-net-pci queue size (256) */
  VRING_ALIGN    = 4096,

  /* Virtio-net header prepended to every TX/RX buffer */
  VNET_HDR_LEN   = 10,

  /* Our per-slot buffer size: header + max Ethernet frame */
  VNET_SLOT_SIZE = VNET_HDR_LEN + 1518,
};

/* Virtqueue descriptor */
typedef struct {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed)) vring_desc_t;

/* Available ring */
typedef struct {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[VRING_SIZE];
} __attribute__((packed)) vring_avail_t;

/* Used ring element */
typedef struct {
  uint32_t id;
  uint32_t len;
} __attribute__((packed)) vring_used_elem_t;

/* Used ring */
typedef struct {
  uint16_t flags;
  uint16_t idx;
  vring_used_elem_t ring[VRING_SIZE];
} __attribute__((packed)) vring_used_t;

/* Virtio-net header (legacy v0.9.5 style, 10 bytes) */
typedef struct {
  uint8_t  flags;
  uint8_t  gso_type;
  uint16_t hdr_len;
  uint16_t gso_size;
  uint16_t csum_start;
  uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

/* -----------------------------------------------------------------------
 * Static virtqueue storage (identity-mapped in protected mode; no IOMMU)
 * Must be 4096-byte aligned – placed in BSS so the linker honours the
 * attribute.
 * --------------------------------------------------------------------- */

#define VRING_TOTAL (sizeof(vring_desc_t) * VRING_SIZE + \
                     sizeof(vring_avail_t) + 6u +        \
                     VRING_ALIGN +                       \
                     sizeof(vring_used_t))

/*
 * Each queue needs:
 *   desc table : VRING_SIZE * 16  = 256*16 = 4096 bytes
 *   avail ring : 4 + VRING_SIZE*2 = 516 bytes  (sits after desc)
 *   used ring  : starts at next VRING_ALIGN boundary = offset 8192
 *                4 + VRING_SIZE*8 = 2052 bytes
 *   Total      : 8192 + 2052 = 10244 bytes -> allocate 12288 (3 pages)
 */
static uint8_t g_rxq_mem[12288] __attribute__((aligned(VRING_ALIGN)));
static uint8_t g_txq_mem[12288] __attribute__((aligned(VRING_ALIGN)));

/* Per-slot TX/RX data buffers (header + frame) */
static uint8_t g_rx_bufs[VRING_SIZE][VNET_SLOT_SIZE];
static uint8_t g_tx_buf[VNET_SLOT_SIZE];

/* Virtqueue book-keeping */
typedef struct {
  vring_desc_t  *desc;
  vring_avail_t *avail;
  vring_used_t  *used;
  uint16_t       size;
  uint16_t       last_used_idx;
} vring_t;

static vring_t g_rxq;
static vring_t g_txq;

/* Driver state */
static unsigned short g_io_base = 0u;
static uint8_t g_mac[NETWORK_MAC_LEN];
static int g_ready = 0;

/* -----------------------------------------------------------------------
 * Memory helpers (no libc in freestanding kernel)
 * --------------------------------------------------------------------- */

static void vnet_memset(void *dst, int val, size_t n) {
  uint8_t *p = (uint8_t *)dst;
  size_t i = 0u;
  for (i = 0u; i < n; ++i) {
    p[i] = (uint8_t)val;
  }
}

static void vnet_memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  size_t i = 0u;
  for (i = 0u; i < n; ++i) {
    d[i] = s[i];
  }
}

/* -----------------------------------------------------------------------
 * Virtqueue initialisation
 * Layout within mem[]:
 *   [0 .. N*16)            descriptors
 *   [N*16 .. N*16+6+N*2)   available ring (flags, idx, ring[N])
 *   (padded to VRING_ALIGN)
 *   [VRING_ALIGN ..)        used ring (flags, idx, ring[N])
 * --------------------------------------------------------------------- */

static void vring_init(vring_t *vr, uint8_t *mem, size_t mem_size, uint16_t size) {
  size_t desc_bytes  = (size_t)size * sizeof(vring_desc_t);
  size_t avail_bytes = 4u + (size_t)size * 2u;
  size_t used_offset = 0u;
  vnet_memset(mem, 0, mem_size);

  vr->desc  = (vring_desc_t *)mem;
  vr->avail = (vring_avail_t *)(mem + desc_bytes);
  /* used ring must be aligned to VRING_ALIGN */
  used_offset = desc_bytes + avail_bytes;
  used_offset = (used_offset + (VRING_ALIGN - 1u)) & ~(unsigned int)(VRING_ALIGN - 1u);
  vr->used  = (vring_used_t *)(mem + used_offset);
  vr->size  = size;
  vr->last_used_idx = 0u;
}

/* Register a virtqueue with the device. */
static void virtio_setup_queue(unsigned short io_base, uint16_t queue_idx, vring_t *vr) {
  uint32_t pfn = 0u;

  vnet_outw((unsigned short)(io_base + VIRTIO_PCI_QUEUE_SELECT), queue_idx);
  vr->size = vnet_inw((unsigned short)(io_base + VIRTIO_PCI_QUEUE_SIZE));
  if (vr->size == 0u) {
    return;
  }
  /* Physical Frame Number: physical address of desc table >> 12 */
  pfn = (uint32_t)((uintptr_t)vr->desc >> 12u);
  vnet_outl((unsigned short)(io_base + VIRTIO_PCI_QUEUE_ADDR), pfn);
}

/* -----------------------------------------------------------------------
 * RX queue: pre-populate all descriptors so the device can fill them
 * --------------------------------------------------------------------- */

static void virtio_net_fill_rx(void) {
  uint16_t i = 0u;

  for (i = 0u; i < VRING_SIZE; ++i) {
    g_rxq.desc[i].addr  = (uint64_t)(uintptr_t)g_rx_bufs[i];
    g_rxq.desc[i].len   = VNET_SLOT_SIZE;
    g_rxq.desc[i].flags = 0x02u; /* VRING_DESC_F_WRITE – device writes here */
    g_rxq.desc[i].next  = 0u;

    g_rxq.avail->ring[i] = i;
  }

  g_rxq.avail->idx = VRING_SIZE;
  /* Kick the RX queue */
  vnet_outw((unsigned short)(g_io_base + VIRTIO_PCI_QUEUE_NOTIFY), VIRTIO_NET_QUEUE_RX);
}

/* -----------------------------------------------------------------------
 * HAL callbacks
 * --------------------------------------------------------------------- */

static network_result_t virtio_net_send(const uint8_t *frame, size_t frame_len) {
  virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)g_tx_buf;
  uint16_t avail_idx = 0u;

  if (frame == 0 || frame_len == 0u) {
    return NETWORK_ERR_BUFFER_INVALID;
  }
  if (frame_len > (size_t)(VNET_SLOT_SIZE - VNET_HDR_LEN)) {
    return NETWORK_ERR_BUFFER_TOO_SMALL;
  }

  vnet_memset(hdr, 0, VNET_HDR_LEN);
  vnet_memcpy(g_tx_buf + VNET_HDR_LEN, frame, frame_len);

  /* Place in TX descriptor slot 0 (simple single-slot TX) */
  g_txq.desc[0].addr  = (uint64_t)(uintptr_t)g_tx_buf;
  g_txq.desc[0].len   = (uint32_t)(VNET_HDR_LEN + frame_len);
  g_txq.desc[0].flags = 0u; /* device reads this */
  g_txq.desc[0].next  = 0u;

  avail_idx = g_txq.avail->idx;
  g_txq.avail->ring[avail_idx % VRING_SIZE] = 0u;
  g_txq.avail->idx = (uint16_t)(avail_idx + 1u);

  /* Memory barrier: ensure writes visible before notify */
  __asm__ __volatile__("" : : : "memory");

  vnet_outw((unsigned short)(g_io_base + VIRTIO_PCI_QUEUE_NOTIFY), VIRTIO_NET_QUEUE_TX);

  /* Poll for TX completion (device updates used ring) */
  {
    uint32_t spin = 0u;
    while (g_txq.used->idx == g_txq.last_used_idx && spin < 100000u) {
      ++spin;
    }
    g_txq.last_used_idx = g_txq.used->idx;
  }

  return NETWORK_OK;
}

static network_result_t virtio_net_recv(uint8_t *frame_out, size_t frame_out_size,
                                         size_t *out_len) {
  uint16_t used_idx = 0u;
  uint32_t desc_id = 0u;
  uint32_t pkt_len = 0u;
  const uint8_t *buf = 0;
  size_t copy_len = 0u;

  if (frame_out == 0 || out_len == 0) {
    return NETWORK_ERR_BUFFER_INVALID;
  }

  *out_len = 0u;

  used_idx = g_rxq.used->idx;
  if (used_idx == g_rxq.last_used_idx) {
    return NETWORK_ERR_RX_EMPTY;
  }

  desc_id = g_rxq.used->ring[g_rxq.last_used_idx % VRING_SIZE].id;
  pkt_len = g_rxq.used->ring[g_rxq.last_used_idx % VRING_SIZE].len;
  g_rxq.last_used_idx = (uint16_t)(g_rxq.last_used_idx + 1u);

  if (pkt_len <= VNET_HDR_LEN) {
    /* Requeue descriptor immediately */
    goto requeue;
  }

  buf = g_rx_bufs[desc_id % VRING_SIZE] + VNET_HDR_LEN;
  copy_len = (size_t)(pkt_len - VNET_HDR_LEN);
  if (copy_len > frame_out_size) {
    copy_len = frame_out_size;
  }
  vnet_memcpy(frame_out, buf, copy_len);
  *out_len = copy_len;

requeue:
  /* Recycle descriptor back into available ring */
  {
    uint16_t avail_idx = g_rxq.avail->idx;
    g_rxq.avail->ring[avail_idx % VRING_SIZE] = (uint16_t)(desc_id);
    g_rxq.avail->idx = (uint16_t)(avail_idx + 1u);
    __asm__ __volatile__("" : : : "memory");
    vnet_outw((unsigned short)(g_io_base + VIRTIO_PCI_QUEUE_NOTIFY), VIRTIO_NET_QUEUE_RX);
  }

  return (*out_len > 0u) ? NETWORK_OK : NETWORK_ERR_RX_EMPTY;
}

static void virtio_net_get_mac(uint8_t mac_out[NETWORK_MAC_LEN]) {
  size_t i = 0u;
  for (i = 0u; i < (size_t)NETWORK_MAC_LEN; ++i) {
    mac_out[i] = g_mac[i];
  }
}

/* -----------------------------------------------------------------------
 * Device descriptor
 * --------------------------------------------------------------------- */

static const network_device_t g_virtio_net_dev = {
  NETWORK_BACKEND_VIRTIO_NET,
  "virtio-net",
  virtio_net_send,
  virtio_net_recv,
  virtio_net_get_mac,
};

/* -----------------------------------------------------------------------
 * Public init: PCI probe + virtio negotiation
 * --------------------------------------------------------------------- */

int virtio_net_init_primary(void) {
  unsigned int bus = 0u, dev = 0u, func = 0u;
  unsigned int found_bus = 0u, found_dev = 0u, found_func = 0u;
  int found = 0;
  unsigned int bar0 = 0u;
  uint8_t device_status = 0u;
  size_t i = 0u;

  /* Scan PCI bus 0 for VID=0x1AF4, DID=0x1000 (virtio legacy net) */
  for (bus = 0u; bus < 2u && !found; ++bus) {
    for (dev = 0u; dev < 32u && !found; ++dev) {
      for (func = 0u; func < 8u && !found; ++func) {
        unsigned int id = pci_cfg_read32(bus, dev, func, 0x00u);
        unsigned int vid = id & 0xFFFFu;
        unsigned int did = (id >> 16u) & 0xFFFFu;

        if (vid == 0x1AF4u && did == 0x1000u) {
          found = 1;
          found_bus  = bus;
          found_dev  = dev;
          found_func = func;
        }
      }
    }
  }

  if (!found) {
    return 0;
  }

  pci_enable_device(found_bus, found_dev, found_func);

  /* Read BAR0 (I/O space base; bit 0 set indicates I/O space) */
  bar0 = pci_cfg_read32(found_bus, found_dev, found_func, 0x10u);
  if ((bar0 & 0x01u) == 0u) {
    return 0; /* BAR0 is MMIO; need I/O space for legacy virtio */
  }
  g_io_base = (unsigned short)(bar0 & 0xFFFCu);

  /* Virtio device initialisation sequence */
  /* 1. Reset */
  vnet_outb((unsigned short)(g_io_base + VIRTIO_PCI_STATUS), 0u);
  /* 2. Acknowledge */
  device_status = VIRTIO_STATUS_ACK;
  vnet_outb((unsigned short)(g_io_base + VIRTIO_PCI_STATUS), device_status);
  /* 3. Driver loaded */
  device_status = (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
  vnet_outb((unsigned short)(g_io_base + VIRTIO_PCI_STATUS), device_status);

  /* 4. Feature negotiation: request MAC address feature only */
  {
    unsigned int host_features = vnet_inl((unsigned short)(g_io_base + VIRTIO_PCI_DEVICE_FEATURES));
    unsigned int guest_features = host_features & VIRTIO_NET_F_MAC;
    vnet_outl((unsigned short)(g_io_base + VIRTIO_PCI_GUEST_FEATURES), guest_features);
  }

  /* 5. Read MAC from device config (only valid if VIRTIO_NET_F_MAC negotiated) */
  for (i = 0u; i < (size_t)NETWORK_MAC_LEN; ++i) {
    g_mac[i] = vnet_inb((unsigned short)(g_io_base + VIRTIO_PCI_CONFIG + (unsigned short)i));
  }

  /* 6. Setup virtqueues */
  vring_init(&g_rxq, g_rxq_mem, sizeof(g_rxq_mem), VRING_SIZE);
  vring_init(&g_txq, g_txq_mem, sizeof(g_txq_mem), VRING_SIZE);
  virtio_setup_queue(g_io_base, VIRTIO_NET_QUEUE_RX, &g_rxq);
  virtio_setup_queue(g_io_base, VIRTIO_NET_QUEUE_TX, &g_txq);

  /* 7. Driver OK */
  device_status = (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
  vnet_outb((unsigned short)(g_io_base + VIRTIO_PCI_STATUS), device_status);

  /* 8. Pre-fill RX descriptors */
  virtio_net_fill_rx();

  /* Register with HAL */
  g_ready = 1;
  network_hal_register_primary(&g_virtio_net_dev);

  return 1;
}
