#ifndef SECUREOS_VIRTIO_NET_H
#define SECUREOS_VIRTIO_NET_H

/**
 * @file virtio_net.h
 * @brief Virtio-net-pci legacy NIC driver header.
 *
 * Purpose:
 *   Declares the public initialization function for the virtio-net-pci driver.
 *   The driver probes PCI bus 0 for a VirtIO 1.0 network device and registers
 *   it as the primary network backend via network_hal_register_primary().
 *
 * Interactions:
 *   - network_hal.c: driver calls network_hal_register_primary() on success.
 *   - kmain.c: virtio_net_init_primary() is called during boot after the
 *     capability table and event bus are initialized.
 *
 * Launched by:
 *   Called from kmain.c.  Not a standalone process; compiled into the kernel.
 */

/* Initialize the virtio-net-pci driver.
 * Probes PCI for a virtio network device (VID=0x1AF4, DID=0x1000).
 * Returns 1 if a device was found and registered, 0 if not found.  */
int virtio_net_init_primary(void);

#endif
