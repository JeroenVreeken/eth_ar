#ifndef _INCLUDE_DTMF_H_
#define _INCLUDE_DTMF_H_

int dtmf_rx(short *smp, int nr, void (*cb)(char *));

int dtmf_init(void);

#endif /* _INCLUDE_DTMF_H_ */
