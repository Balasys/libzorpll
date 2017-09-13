#ifndef ZCV_STREAMTEE_H
#define ZCV_STREAMTEE_H

#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

ZStream *z_stream_tee_new(ZStream *child, ZStream *fork, GIOCondition tee_direction);

#ifdef __cplusplus
}
#endif

#endif
