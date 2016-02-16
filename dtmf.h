#ifndef _INCLUDE_DTMF_H_
#define _INCLUDE_DTMF_H_

int dtmf_decode(short *samples, int nr, void (*cb)(char *));

#endif /* _INCLUDE_DTMF_H_ */
