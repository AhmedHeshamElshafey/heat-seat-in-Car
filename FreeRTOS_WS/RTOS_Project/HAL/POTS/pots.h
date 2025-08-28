

#ifndef HAL_POTS_POTS_H_
#define HAL_POTS_POTS_H_

#include <stdint.h>

#define POT_MAX_VALUE   4096


void POT_init(void);
uint32_t POT_getValue();




#endif /* HAL_POTS_POTS_H_ */
