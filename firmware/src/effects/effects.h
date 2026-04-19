#ifndef _conez_effects_h
#define _conez_effects_h

void CIRCLE_effect(void);
void SOS_effect(void);
void SOS_effect2(void);

// Zero all per-effect state (prev_sec, state machines, targets). Call this
// when switching between effects so stale values from the previous effect
// don't trigger a spurious fire or skip on the first tick of the new one.
void effects_reset_all(void);

#endif