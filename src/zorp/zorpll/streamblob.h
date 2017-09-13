/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_STREAMBLOB_H
#define ZORP_STREAMBLOB_H 1

#include <zorpll/blob.h>

#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

ZStream *z_stream_blob_new(ZBlob *blob, const gchar *name);

#ifdef __cplusplus
}
#endif

#endif
