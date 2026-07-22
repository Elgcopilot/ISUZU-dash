#ifndef CAN_MGR_H
#define CAN_MGR_H

void can_init();
void *can_rx_thread(void *arg);
void *can_tx_obd_thread(void *arg); // Add this
void scx_can_init();
void *scx_can_thread(void *arg);
void *simulator_thread(void *arg);

#endif
