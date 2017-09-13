/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/zcp.h>

#include <stdlib.h>

#include <zorpll/log.h>
#include <zorpll/streamline.h>

#define ZAS_ERROR "zas.error"

#define ZCP_STATE_IDLE	0
#define ZCP_STATE_WRITING     1

#define ZCP_STATE_READING_HEADER 10
#define ZCP_STATE_READING_VALUE  11
#define ZCP_STATE_READING_TRAILER 12

#define ZCP_MAX_HEADERS       256
#define ZCP_MAX_HEADER_LENGTH 16384

struct _ZCPContext
{
  ZStream *stream;
  gint write_state;
  gint read_state;
  gboolean error;

  /* command currently being parsed */
  guint pending_session_id;
  ZCPCommand *pending;

  /* headers being parsed */
  GString *key;
  GString *value;
  gssize valuelen;

  /* write buffer */
  GString *buffer;
  guint bufpos;
};

/* ZCPCommand */

static inline gint
z_cp_command_get_headers_count(ZCPCommand *self)
{
  return z_header_set_get_count(&self->headers);
}

ZCPCommand *
z_cp_command_new(const gchar *cmd)
{
  ZCPCommand *self = g_new0(ZCPCommand, 1);

  z_enter();
  z_header_set_init(&self->headers);
  if (cmd)
    self->command = g_string_new(cmd);
  z_return(self);
}

ZCPCommand *
z_cp_command_new_accept(const gchar *welcome, GSList *groups)
{
  ZCPCommand *cmd;

  cmd = z_cp_command_new("ACCEPT");
  if (welcome)
    z_cp_command_add_header(cmd, g_string_new("Result"), g_string_new(welcome), FALSE);
  while (groups)
    {
      z_cp_command_add_header(cmd, g_string_new("Group"), g_string_new(((GString *) groups->data)->str), TRUE);
      groups = g_slist_next(groups);
    }
  return cmd;
}

ZCPCommand *
z_cp_command_new_reject(const gchar *reason)
{
  ZCPCommand *cmd;

  cmd = z_cp_command_new("REJECT");
  if (reason)
    z_cp_command_add_header(cmd, g_string_new("Reason"), g_string_new(reason), FALSE);
  return cmd;
}

void
z_cp_command_free(ZCPCommand *self)
{
  z_enter();
  if (self)
    {
      if (self->command)
        {
          g_string_free(self->command, TRUE);
          self->command = NULL;
        }
      z_header_set_destroy(&self->headers);
      g_free(self);
    }
  z_return();
}


/* ZCPContext */

GIOStatus
z_cp_context_read(ZCPContext *self, guint *session_id, ZCPCommand **cmd)
{
  gchar *buf;
  gsize buflen = ZCP_MAX_HEADER_LENGTH;
  gchar *tmpbuf;
  gint len;
  GIOStatus ret = G_IO_STATUS_ERROR;
  gchar **split;

  z_enter();
  if (self->error)
    z_return(G_IO_STATUS_ERROR);
  while (1)
    {
      switch (self->read_state)
        {
        case ZCP_STATE_IDLE:
          /* read command */
          ret = z_stream_line_get(self->stream, &buf, &buflen, NULL);
          if (ret != G_IO_STATUS_NORMAL)
            {
              if (ret == G_IO_STATUS_ERROR)
                self->error = TRUE;
              z_return(ret);
            }
          /* eck... modifying the buffer in z_stream_line */
          buf[buflen] = 0;
          split = g_strsplit(buf, " ", 2);

          if (split[0] == NULL ||   /* Session ID */
              split[1] == NULL)     /* Message type */
            {
              z_log(NULL, ZAS_ERROR, 3, "Incomplete command header; header='%s'", buf);
              self->error = TRUE;
              z_return(G_IO_STATUS_ERROR);
            }
          else
            {
              self->pending = z_cp_command_new(split[1]);
              self->pending_session_id = atoi(split[0]);
              self->read_state = ZCP_STATE_READING_HEADER;
            }
          g_strfreev(split);
          break;

        case ZCP_STATE_READING_HEADER:
          ret = z_stream_line_get(self->stream, &buf, &buflen, NULL);
          if (ret != G_IO_STATUS_NORMAL)
            {
              if (ret == G_IO_STATUS_ERROR)
                self->error = TRUE;
              z_return(ret);
            }
          if (buflen > 2)
            {
              if (z_cp_command_get_headers_count(self->pending) >= ZCP_MAX_HEADERS)
                {
                  /* too many headers */
                  self->error = TRUE;
                  z_log(NULL, ZAS_ERROR, 3, "Too many headers; max='%d'", ZCP_MAX_HEADERS);
                  z_return(G_IO_STATUS_ERROR);
                }
              if (buf[0] == '[')
                {
                  if (buf[1] == ']')
                    {
                      len = -1;
                      tmpbuf = buf + 1;
                    }
                  else
                    {
                      len = strtol(buf + 1, &tmpbuf, 10);
                      if (len < 0)
                        {
                          len = 0;
                        }
                      else if (len > ZCP_MAX_HEADER_LENGTH)
                        {
                          self->error = TRUE;
                          z_log(NULL, ZAS_ERROR, 3, "Header too long; len='%d'", len);
                          z_return(G_IO_STATUS_ERROR);
                        }
                    }
                  if (tmpbuf[0] == ']' && (tmpbuf - buf) < (gssize)buflen)
                    {
                      self->valuelen = len;
                      self->key = g_string_new_len(tmpbuf + 1, buflen + buf - tmpbuf - 1);
                      self->read_state = ZCP_STATE_READING_VALUE;
                      self->value = g_string_sized_new(len);
                    }
                  else
                    {
                      self->error = TRUE;
                      z_log(NULL, ZAS_ERROR, 3, "Invalid header length specification; line='%.*s'", (gint) buflen, buf);
                      z_return(G_IO_STATUS_ERROR);
                    }
                }
              else
                {
                  self->error = TRUE;
                  z_log(NULL, ZAS_ERROR, 3, "Invalid header length specification; line='%.*s'", (gint) buflen, buf);
                  z_return(G_IO_STATUS_ERROR);
                }
            }
          else
            {
              if (buflen != 0)
                {
                  /* line with a single character ? */
                  self->error = TRUE;
                  z_log(NULL, ZAS_ERROR, 3, "Buffer too short to contain a valid header; line='%.*s'", (gint) buflen, buf);
                  z_return(G_IO_STATUS_ERROR);
                }
              else
                {
                  /* complete packet, closing empty line received */
                  *cmd = self->pending;
                  *session_id = self->pending_session_id;
                  self->pending = NULL;
                  self->read_state = ZCP_STATE_IDLE;
                  z_return(G_IO_STATUS_NORMAL);
                }
            }
          break;

        case ZCP_STATE_READING_VALUE:
          if (self->valuelen == -1)
            {
              /* unspecified length */
              ret = z_stream_line_get(self->stream, &buf, &buflen, NULL);
              if (ret != G_IO_STATUS_NORMAL)
                {
                  if (ret == G_IO_STATUS_ERROR)
                    self->error = TRUE;
                  z_return(ret);
                }
              self->value = g_string_new_len(buf, buflen);
              z_cp_command_add_header(self->pending, self->key, self->value, TRUE);
              self->key = self->value = NULL;
              self->read_state = ZCP_STATE_READING_HEADER;
            }
          else if (self->valuelen > 0)
            {
              gchar buf2[ZCP_MAX_HEADER_LENGTH];

              ret = z_stream_read(self->stream, buf2, MIN((gsize)self->valuelen, (gsize)sizeof(buf2)), &buflen, NULL);
              if (ret == G_IO_STATUS_NORMAL)
                {
                  self->valuelen -= buflen;
                  self->value = g_string_append_len(self->value, buf2, buflen);
                }
              else
                {
                  if (ret == G_IO_STATUS_ERROR)
                    self->error = TRUE;
                  z_return(ret);
                }
            }

          if (self->valuelen == 0)
            {
              /* no remaining header data, add to header list */
              z_cp_command_add_header(self->pending, self->key, self->value, TRUE);
              self->key = self->value = NULL;
              self->read_state = ZCP_STATE_READING_TRAILER;
            }
          break;

        case ZCP_STATE_READING_TRAILER:
          ret = z_stream_line_get(self->stream, &buf, &buflen, NULL);
          if (ret != G_IO_STATUS_NORMAL)
            {
              if (ret == G_IO_STATUS_ERROR)
                self->error = TRUE;
              z_return(ret);
            }
          if (buflen != 0)
            {
              self->error = TRUE;
              z_log(NULL, ZAS_ERROR, 3, "Trailer line must be empty; line='%.*s'", (gint) buflen, buf);
              z_return(G_IO_STATUS_ERROR);
            }
          self->read_state = ZCP_STATE_READING_HEADER;
          break;

        default:
          break;
        }
    }
  z_return(ret);
}

static gboolean
z_cp_context_format_command(ZCPContext *self, guint session_id, ZCPCommand *cmd)
{
  GList *hdrs, *p;

  z_enter();
  g_string_truncate(self->buffer, 0);
  if (cmd == NULL)
    z_return(FALSE);
  g_string_append_printf(self->buffer, "%d %s\n", session_id, cmd->command->str);
  hdrs = z_header_set_get_all_headers(&cmd->headers);
  for (p = hdrs; p; p = p->next)
    {
      ZCPHeader *header;

      header = static_cast<ZCPHeader *>(p->data);
      g_string_append_printf(self->buffer,
                             "[%d]%s\n",
                             (gint) header->value->len,
                             header->key->str);
      g_string_append_len(self->buffer, header->value->str, header->value->len);
      g_string_append_c(self->buffer, '\n');
    }
  g_list_free(hdrs);
  g_string_append_c(self->buffer, '\n');
  z_return(TRUE);
}

GIOStatus
z_cp_context_write(ZCPContext *self, guint session_id, ZCPCommand *cmd)
{
  GIOStatus ret;
  gsize write_len;

  z_enter();
  if ((self->error) ||
      (self->write_state != ZCP_STATE_IDLE && self->write_state != ZCP_STATE_WRITING))
    z_return(G_IO_STATUS_ERROR);

  if (self->write_state == ZCP_STATE_IDLE)
    {
      if (!z_cp_context_format_command(self, session_id, cmd))
        z_return(G_IO_STATUS_ERROR);
      self->write_state = ZCP_STATE_WRITING;
    }

  ret = z_stream_write(self->stream,
                       self->buffer->str + self->bufpos,
                       self->buffer->len - self->bufpos,
                       &write_len,
                       NULL);
  switch (ret)
    {
    case G_IO_STATUS_NORMAL:
      self->bufpos += write_len;
      if (self->bufpos < self->buffer->len)
        {
          ret = G_IO_STATUS_AGAIN;
        }
      else
        {
          g_string_truncate(self->buffer, 0);
          self->bufpos = 0;
          self->write_state = ZCP_STATE_IDLE;
        }
      break;

    case G_IO_STATUS_ERROR:
      self->error = TRUE;

    default:
      break;
    }
  z_return(ret);
}

ZCPContext *
z_cp_context_new(ZStream *stream)
{
  ZCPContext *self = g_new0(ZCPContext, 1);

  z_stream_ref(stream);
  self->stream = stream;
  self->buffer = g_string_sized_new(128);
  return self;
}

void
z_cp_context_destroy(ZCPContext *self, gboolean close_stream)
{
  if (close_stream)
    {
      z_stream_close(self->stream, NULL);
    }
  z_stream_unref(self->stream);
  g_string_free(self->buffer, TRUE);
  g_free(self);
}
