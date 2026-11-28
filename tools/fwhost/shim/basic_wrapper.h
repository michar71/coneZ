// Host shim for firmware/src/basic/basic_wrapper.h: the 16 script params.
#ifndef FWHOST_BASIC_WRAPPER_H
#define FWHOST_BASIC_WRAPPER_H
int  get_basic_param(int paramID);            // 0 for an out-of-range id
bool set_basic_param(int paramID, int val);   // false for an out-of-range id
#endif
