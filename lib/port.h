#ifndef _PORT_H
#define _PORT_H

#include "arch.h"
#include "port-user.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void *port_alloc(size_t size)
{
	return port_alloc_x(size, PORT_DEFAULT_ALLOC_FLAG);
}

#ifdef __cplusplus
}
#endif
#endif /* _PORT_H */
