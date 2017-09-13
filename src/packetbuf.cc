#include <zorpll/packetbuf.h>
#include <zorpll/log.h>

/**
 * Logs the contents of a buffer, prepended with a header if title is not NULL.
 *
 * @param[in] session_id parameter for z_log
 * @param[in] klass parameter for z_log
 * @param[in] level parameter for z_log
 * @param[in] self this
 * @param[in] title optional title to prepend
 **/
void
z_pktbuf_dump(const gchar *session_id, const gchar *klass, int level, ZPktBuf *self, const gchar *title)
{
  if (title)
    {
      z_log(session_id, klass, level,
            "Packet buffer dump follows; title='%s', borrowed='%s', data='%p', "
            "allocated='%" G_GSIZE_FORMAT"', length='%" G_GSIZE_FORMAT "', "
            "pos='%" G_GSIZE_FORMAT "'",
            title, YES_NO_STR(self->flags & Z_PB_BORROWED), self->data,
            self->allocated, self->length, self->pos);
    }
  z_log_data_dump(session_id, klass, level, (gchar *) self->data, self->length);
}

/**
 * Set the data/length fields of p to the specified data block by copying the
 * data to the internal buffer of ZPktBuf.
 *
 * @param[in] self 'this'
 * @param[in] data pointer to the data block
 * @param[in] length length of the data block
 *
 * @returns TRUE to be able to chain it.
 **/
gboolean
z_pktbuf_copy(ZPktBuf *self, const void *data, gsize length)
{
  z_pktbuf_resize(self, length);
  if (self->pos > length)
    self->pos = length;
  self->length = length;
  memcpy(self->data, data, length);
  return TRUE;
}

/**
 * Set the data/length fields of p to the specified data block without
 * copying and actually using data as a pointer.
 *
 * @param[in] self 'this'
 * @param[in] data pointer to the data block
 * @param[in] length length of the data block
 * @param[in] is_borrowed is data to be freed automatically or not
 *
 * If is_borrowed is set, then
 * it is assumed that data cannot be freed using g_free().
 **/
void
z_pktbuf_relocate(ZPktBuf *self, void *data, gsize length, gboolean is_borrowed)
{
  if (self->data && !(self->flags & Z_PB_BORROWED))
    g_free(self->data);
  if (self->pos > length)
    self->pos = length;
  self->data = static_cast<guchar *>(data);
  self->length = self->allocated = length;
  if (is_borrowed)
    self->flags = static_cast<ZPktBufFlags>(self->flags | Z_PB_BORROWED);
  else
    self->flags = static_cast<ZPktBufFlags>(self->flags & ~Z_PB_BORROWED);
}

/**
 * Tries to ensure that at least size bytes is available in self.
 *
 * @param[in] self this
 * @param[in] size requested length in bytes
 *
 * @note This may fail if the buffer is initialised from an external
 * and not relocatable data.
 **/
void
z_pktbuf_resize(ZPktBuf *self, gsize size)
{
  if (size > self->allocated)
    {
      /* We don't have to realloc borrowed memory pointer. */
      g_assert(!(self->flags & Z_PB_BORROWED));

      self->data = static_cast<guchar *>(g_realloc(self->data, size));
      self->allocated = size;
    }
  if (self->length > size)
    self->length = size;
  if (self->pos > size)
    self->pos = size;
}

/**
 * Tries to ensures that size bytes are available in self starting from the
 * current position.
 *
 * @param[in] self this
 * @param[in] size amount of bytes needed
 *
 * @todo FIXME: Why it's setting self->length?
 * 
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_set_available(ZPktBuf *self, gsize size)
{
  if (self->length >= self->pos + size)
    return TRUE;
  self->length = self->pos + size;
  z_pktbuf_resize(self, self->pos + size);
  return TRUE;
}

/**
 * Tries to append a byte array to self.
 *
 * @param[in] self this
 * @param[in] data buffer to append
 * @param[in] length length of data
 *
 * @returns TRUE
 **/
gboolean
z_pktbuf_append(ZPktBuf *self, const void *data, gsize length)
{
  z_pktbuf_resize(self, self->length + length);
  g_memmove(self->data + self->length, data, length);
  self->length += length;
  return TRUE;
}

/**
 * Tries to insert a byte array into self.
 *
 * @param[in] self this
 * @param[in] pos position to insert to
 * @param[in] data data to insert
 * @param[in] length length of data
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_insert(ZPktBuf *self, gsize pos, const void *data, gsize length)
{
  z_pktbuf_resize(self, self->length + length);

  g_memmove(self->data + pos + length, self->data + pos, self->length - pos);
  g_memmove(self->data + pos, data, length);
  self->length += length;
  return TRUE;
}

gboolean
z_pktbuf_data_equal(ZPktBuf *lhs, ZPktBuf *rhs)
{
  return lhs->length == rhs->length &&
         !memcmp(lhs->data, rhs->data, rhs->length);
}

/**
 * Create a new ZPktBuf instance, set its length to 0 and its data ptr to NULL
 *
 * @returns ZPktBuf* pointer to the new instance
 **/
ZPktBuf *
z_pktbuf_new(void)
{
  ZPktBuf *self;

  z_enter();
  self = g_new0(ZPktBuf, 1);
  z_refcount_set(&self->ref_cnt, 1);
  z_return(self);
}

/**
 * Create a new ZPktBuf instance pointing to a slice of another instance.
 *
 * @param[in] parent ZPktBuf instance to point to
 * @param[in] pos beginning of slice (offset from the beginning of parent->data)
 * @param[in] len length of slice
 * 
 * @note The parent instance must hold a non-relocatable buffer, otherwise
 * it couldn't be guaranteed that a) the original instance won't get relocated
 * b) both ZPktBuf instances manipulate the same data.
 *
 * @note It turned out so that we must permit this - no way to
 * do it without hacking.
 *
 * @returns ZPktBuf* pointer to the new instance
 **/
ZPktBuf *
z_pktbuf_part(ZPktBuf *parent, gsize pos, gsize len)
{
  ZPktBuf *self = NULL;

  z_enter();
  self = g_new0(ZPktBuf, 1);
  z_refcount_set(&self->ref_cnt, 1);
  self->data = parent->data + pos;
  self->allocated = self->length = MIN(len, parent->length - pos);
  self->flags = Z_PB_BORROWED;
  z_return(self);
}

/**
 * Split a ZPktBuf object into two at a specified position.
 *
 * @param[in] self ZPktBuf object to split. Will contain the first part of the buffer after the split
 * @param[in] at The position to split at
 *
 * @returns ZPktBuf* object with the second part of the buffer, starting with the character specified by the position
 */
ZPktBuf *
z_pktbuf_split(ZPktBuf *self, gsize at)
{
  ZPktBuf *ret;

  ret = z_pktbuf_new();

  z_pktbuf_copy(ret, self->data+at, self->length-at);
  z_pktbuf_resize(self, at);

  return ret;
}

/**
 * Increment the reference counter for self.
 *
 * @param[in] self packet
 *
 * @returns Pointer to the instance
 **/
ZPktBuf *
z_pktbuf_ref(ZPktBuf *self)
{
  z_refcount_inc(&self->ref_cnt);
  return self;
}

/**
 * Decrements the reference counter for self and once it reaches 0, it
 * deallocates the buffer if present, then the instance itself.
 *
 * @param[in] self 'this'
 **/
void 
z_pktbuf_unref(ZPktBuf *self)
{
  z_enter();
  if (self && z_refcount_dec(&self->ref_cnt))
    {
      if (self->data && !(self->flags & Z_PB_BORROWED))
        g_free(self->data);

      g_free(self);
    }
  z_return();
}

/**
 * Construct a new ZPktBuf and initialize it with the contents of a GString.
 *
 * @param[in] str GString instance containing the data to initialize the packet with.
 *
 **/
ZPktBuf *
z_pktbuf_new_from_gstring(const GString * const str)
{
  ZPktBuf *pkt = z_pktbuf_new();

  z_pktbuf_put_u8s(pkt, str->len, (guint8 *) str->str);

  return pkt;
}


/**
 * Moves the current position in the buffer
 *
 * @param[in] self this
 * @param[in] whence seek type
 * @param[in] pos amount to seek
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_seek(ZPktBuf *self, GSeekType whence, gssize pos)
{
  switch (whence)
    {
    case G_SEEK_CUR:
      if ((self->pos + pos) > self->length || (((gssize)self->pos) + pos) < 0)
        return FALSE;
      self->pos += pos;
      break;

    case G_SEEK_SET:
      if (pos > (gssize)self->length || pos < 0)
        return FALSE;
      self->pos = pos;
      break;

    case G_SEEK_END:
      if ((pos > 0) || ((gssize)self->length < -pos))
        return FALSE;
      self->pos = self->length + pos;
      break;
    }
  return TRUE;
}


/*
 * note on Doxygen comments: This is an ugly hack to avoid repetition,
 * but it's the best I could come up with. 
 *
 * Brief descriptions appear twice because the brief description had
 * to be written explicitly, otherwise the entire text inserted by
 * @copydoc would be the "brief description".
 *
 * Perhaps this could be redone with DISTRIBUTE_GROUP_DOC.
 */

/**
 * Read an unsigned X-bit number from a ZPktBuf.
 *
 * @copydoc z_pktbuf_get_u16
 **/
gboolean
z_pktbuf_get_u8(ZPktBuf *self, guint8 *res)
{
  if (z_pktbuf_available(self) < 1)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint8; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"'", self->length, self->pos);
      return FALSE;
    }

  if (res)
    res[0] = *(guint8*)(self->data + self->pos);
  self->pos++;
  return TRUE;
}

/**
 * Read an unsigned X-bit number from a ZPktBuf.
 *
 * @param[in]  self this
 * @param[in]  e endianness of the stored value (missing if X=8)
 * @param[out] res pointer to store the value to
 *
 * @note X is the number at the end of the function name.
 *
 * Reads an unsigned X-bit number from the current position of self and
 * stores it to res if it's not NULL, moving the current position pointer to
 * the next available position.
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_get_u16(ZPktBuf *self, gint e, guint16 *res)
{
  if (z_pktbuf_available(self) < 2)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint16; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"'", self->length, self->pos);
      return FALSE;
    }

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        res[0] = *(guint16*)(self->data + self->pos);
      else
        res[0] = GUINT16_SWAP_LE_BE(*(guint16*)(self->data + self->pos));
    }
  self->pos += 2;
  return TRUE;
}

/**
 * Read an unsigned X-bit number from a ZPktBuf.
 *
 * @copydoc z_pktbuf_get_u16
 **/
gboolean
z_pktbuf_get_u32(ZPktBuf *self, gint e, guint32 *res)
{
  if (z_pktbuf_available(self) < 4)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint32; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"'", self->length, self->pos);
      return FALSE;
    }

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        res[0] = *(guint32*)(self->data + self->pos);
      else
        res[0] = GUINT32_SWAP_LE_BE(*(guint32*)(self->data + self->pos));
    }
  self->pos += 4;
  return TRUE;
}

/**
 * Read an unsigned X-bit number from a ZPktBuf.
 *
 * @copydoc z_pktbuf_get_u16
 **/
gboolean
z_pktbuf_get_u64(ZPktBuf *self, gint e, guint64 *res)
{
  if (z_pktbuf_available(self) < 8)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint64; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"'", self->length, self->pos);
      return FALSE;
    }

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        res[0] = *(guint64*)(self->data + self->pos);
      else
        res[0] = GUINT64_SWAP_LE_BE(*(guint64*)(self->data + self->pos));
    }
  self->pos += 8;
  return TRUE;
}

/**
 * Write an unsigned X-bit number to a ZPktBuf.
 *
 * @copydoc z_pktbuf_put_u16
 **/
gboolean
z_pktbuf_put_u8(ZPktBuf *self, guint8 d)
{
  z_pktbuf_set_available(self, 1);

  *(guint8*)(self->data + self->pos) = d;
  self->pos++;
  return TRUE;
}

/**
 * Write an unsigned X-bit number to a ZPktBuf.
 *
 * @param[in] self this
 * @param[in] e endianness to use for storing the value (missing if X=8)
 * @param[in] d value to store
 *
 * @note X is the number at the end of the function name.
 *
 * Writes an unsigned X-bit number to the current position of self, moving
 * the current position pointer to the next available position.
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_put_u16(ZPktBuf *self, gint e, guint16 d)
{
  z_pktbuf_set_available(self, 2);

  if (e == G_HOST_ENDIAN)
    *(guint16*)(self->data + self->pos) = d;
  else
    *(guint16*)(self->data + self->pos) = GUINT16_SWAP_LE_BE(d);
  self->pos += 2;
  return TRUE;
}

/**
 * Write an unsigned X-bit number to a ZPktBuf.
 *
 * @copydoc z_pktbuf_put_u16
 **/
gboolean
z_pktbuf_put_u32(ZPktBuf *self, gint e, guint32 d)
{
  z_pktbuf_set_available(self, 4);

  if (e == G_HOST_ENDIAN)
    *(guint32*)(self->data + self->pos) = d;
  else
    *(guint32*)(self->data + self->pos) = GUINT32_SWAP_LE_BE(d);
  self->pos += 4;
  return TRUE;
}

/**
 * Write an unsigned X-bit number to a ZPktBuf.
 *
 * @copydoc z_pktbuf_put_u16
 **/
gboolean
z_pktbuf_put_u64(ZPktBuf *self, gint e, guint64 d)
{
  z_pktbuf_set_available(self, 8);

  if (e == G_HOST_ENDIAN)
    *(guint64*)(self->data + self->pos) = d;
  else
    *(guint64*)(self->data + self->pos) = GUINT64_SWAP_LE_BE(d);
  self->pos += 8;
  return TRUE;
}

/**
 * Read an array of n unsigned X-bit numbers from a ZPktBuf.
 *
 * @copydoc z_pktbuf_get_u16s
 **/
gboolean
z_pktbuf_get_u8s(ZPktBuf *self, gsize n, guint8 *res)
{
  if (z_pktbuf_available(self) < n)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint8 array; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"', req_len='%" G_GSIZE_FORMAT"'", self->length, self->pos, n);
      return FALSE;
    }

  if (res)
    memcpy(res, self->data + self->pos, n);
  self->pos += n;
  return TRUE;

}

/**
 * Read an array of n unsigned X-bit numbers from a ZPktBuf.
 *
 * @param[in]  self this
 * @param[in]  e endianness of the stored value (missing if X=8)
 * @param[in]  n number of values to read
 * @param[out] res pointer to store the value to
 *
 * @note X is the number at the end of the function name.
 *
 * Reads an array of n unsigned X-bit numbers from the current position of
 * self and stores them to res if it's not NULL, moving the current position
 * pointer to the next available position.
 *
 * @returns TRUE on success
 */
gboolean
z_pktbuf_get_u16s(ZPktBuf *self, gint e, gsize n, guint16 *res)
{
  guint i;

  n <<= 1;
  if (z_pktbuf_available(self) < n)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint16 array; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"', req_len='%" G_GSIZE_FORMAT"'", self->length, self->pos, n);
      return FALSE;
    }

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        {
          memcpy(res, self->data + self->pos, n);
        }
      else
        {
          for (i = 0; i < n; i += 2)
            *(guint16*)((guint8*)res + i) = GUINT16_SWAP_LE_BE(*(guint16*)(self->data + self->pos + i));
        }
    }
  self->pos += n;
  return TRUE;
}

/**
 * Read an array of n unsigned X-bit numbers from a ZPktBuf.
 *
 * @copydoc z_pktbuf_get_u16s
 **/
gboolean
z_pktbuf_get_u32s(ZPktBuf *self, gint e, gsize n, guint32 *res)
{
  guint i;

  n <<= 2;
  if (z_pktbuf_available(self) < n)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint32 array; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"', req_len='%" G_GSIZE_FORMAT"'", self->length, self->pos, n);
      return FALSE;
    }

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        {
          memcpy(res, self->data + self->pos, n);
        }
      else
        {
          for (i = 0; i < n; i += 4)
            *(guint32*)((guint8*)res + i) = GUINT32_SWAP_LE_BE(*(guint32*)(self->data + self->pos + i));
        }
    }
  self->pos += n;
  return TRUE;
}

/**
 * Read an array of n unsigned X-bit numbers from a ZPktBuf.
 *
 * @copydoc z_pktbuf_get_u16s
 **/
gboolean
z_pktbuf_get_u64s(ZPktBuf *self, gint e, gsize n, guint64 *res)
{
  guint i;

  n <<= 3;
  if (z_pktbuf_available(self) < n)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing uint64 array; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"', req_len='%" G_GSIZE_FORMAT"'", self->length, self->pos, n);
      return FALSE;
    }

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        {
          memcpy(res, self->data + self->pos, n);
        }
      else
        {
          for (i = 0; i < n; i += 8)
            *(guint64*)((guint8*)res + i) = GUINT64_SWAP_LE_BE(*(guint64*)(self->data + self->pos + i));
        }
    }
  self->pos += n;
  return TRUE;
}

/**
 * Write an array of n unsigned X-bit numbers into a ZPktBuf.
 *
 * @copydoc z_pktbuf_put_u16s
 **/
gboolean
z_pktbuf_put_u8s(ZPktBuf *self, gsize n, const guint8 *res)
{
  z_pktbuf_set_available(self, n);

  if (res)
    memcpy(self->data + self->pos, res, n);
  self->pos += n;
  return TRUE;

}

/**
 * Write an array of n unsigned X-bit numbers into a ZPktBuf.
 *
 * @param[in] self this
 * @param[in] e endianness to use for storing the values (missing if X=8)
 * @param[in] n number of values to write
 * @param[in] res array of values to write
 *
 * @note X is the number at the end of the function name.
 *
 * Writes an array of n unsigned X-bit numbers to the current position of
 * self, moving the current position pointer to the next available position.
 *
 * @returns TRUE on success
 */
gboolean
z_pktbuf_put_u16s(ZPktBuf *self, gint e, gsize n, const guint16 *res)
{
  guint i;

  n <<= 1;
  z_pktbuf_set_available(self, n);

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        {
          memcpy(self->data + self->pos, res, n);
        }
      else
        {
          for (i = 0; i < n; i += 2)
            *(guint16*)(self->data + self->pos + i) = GUINT16_SWAP_LE_BE(*(guint16*)((guint8*)res + i));
        }
    }
  self->pos += n;
  return TRUE;
}

/**
 * Write an array of n unsigned X-bit numbers into a ZPktBuf.
 *
 * @copydoc z_pktbuf_put_u16s
 **/
gboolean
z_pktbuf_put_u32s(ZPktBuf *self, gint e, gsize n, const guint32 *res)
{
  guint i;

  n <<= 2;
  z_pktbuf_set_available(self, n);

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        {
          memcpy(self->data + self->pos, res, n);
        }
      else
        {
          for (i = 0; i < n; i += 4)
            *(guint32*)(self->data + self->pos + i) = GUINT32_SWAP_LE_BE(*(guint32*)((guint8*)res + i));
        }
    }
  self->pos += n;
  return TRUE;
}

/**
 * Write an array of n unsigned X-bit numbers into a ZPktBuf.
 *
 * @copydoc z_pktbuf_put_u16s
 **/
gboolean
z_pktbuf_put_u64s(ZPktBuf *self, gint e, gsize n, const guint64 *res)
{
  guint i;

  n <<= 3;
  z_pktbuf_set_available(self, n);

  if (res)
    {
      if (e == G_HOST_ENDIAN)
        {
          memcpy(self->data + self->pos, res, n);
        }
      else
        {
          for (i = 0; i < n; i += 8)
            *(guint64*)(self->data + self->pos + i) = GUINT64_SWAP_LE_BE(*(guint64*)((guint8*)res + i));
        }
    }
  self->pos += n;
  return TRUE;
}

/**
 * Read an 8-bit boolean value from a ZPktBuf.
 *
 * @param[in]  self ZPktBuf instance
 * @param[out] res the value will be returned here
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_get_boolean(ZPktBuf *self, gboolean *res)
{
  if (z_pktbuf_available(self) < 1)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing boolean; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"'", self->length, self->pos);
      return FALSE;
    }

  if (res)
    res[0] = !!(*(guint8*)(self->data + self->pos));
  self->pos++;
  return TRUE;
}

/**
 * Write an 8-bit boolean value to a ZPktBuf.
 *
 * @param[in] self ZPktBuf instance
 * @param[in] res value to write
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_put_boolean(ZPktBuf *self, gboolean res)
{
  if (!z_pktbuf_set_available(self, 1))
    return FALSE;

  *(guint8*)(self->data + self->pos) = res ? 1 : 0;
  self->pos++;
  return TRUE;
}


/**
 * Read a 16-bit boolean value from a ZPktBuf.
 *
 * @param[in]  self ZPktBuf instance
 * @param[out] res the value will be returned here
 *
 * @returns TRUE on success
 **/
gboolean
z_pktbuf_get_boolean16(ZPktBuf *self, gboolean *res)
{
  if (z_pktbuf_available(self) < 2)
    {
      z_log(NULL, CORE_DEBUG, 7, "Error parsing boolean16; length='%" G_GSIZE_FORMAT "', pos='%" G_GSIZE_FORMAT"'", self->length, self->pos);
      return FALSE;
    }

  if (res)
    res[0] = !!(*(guint16*)(self->data + self->pos));
  self->pos += 2;
  return TRUE;
}

/**
 * @brief Append a string to the buffer, properly updating the position.
 *
 * @param[in] self ZPktBuf instance
 * @param[in] str the string to append
 * @return TRUE on success
 */
gboolean
z_pktbuf_put_string(ZPktBuf *self, const gchar *str)
{
  return z_pktbuf_put_u8s(self, strlen(str), (guint8 *) str);
}

/**
 * @brief Append a pktbuf while consuming it
 * @param[in] self ZPktBuf instance
 * @param[in] other ZPktBuf to append, will be consumed
 * @return self
 */
ZPktBuf *
z_pktbuf_append_pktbuf(ZPktBuf *self, ZPktBuf *other)
{
  z_pktbuf_append(self, other->data, other->length);
  z_pktbuf_unref(other);
  return self;
}
