#ifndef DGE_TIME_H
#define DGE_TIME_H

/* Call once per frame before anything reads dt. */
void dge_time_tick(void);

/* Seconds elapsed since last frame. Capped at 0.05s to avoid spiral of death. */
float dge_time_delta(void);

/* Total seconds since the engine started. */
double dge_time_elapsed(void);

/* Frames rendered so far. */
unsigned int dge_time_frame(void);

#endif /* DGE_TIME_H */
