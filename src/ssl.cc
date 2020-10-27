/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

/**
 * @file
 * interface between Zorp and the SSL library
 **/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <zorpll/ssl.h>
#include <zorpll/log.h>
#include <zorpll/thread.h>

#include <memory>

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <openssl/err.h>
#undef X509_NAME
#include <openssl/x509v3.h>
#include <string.h>

#if ZORPLIB_ENABLE_SSL_ENGINE
#include <openssl/engine.h>
gchar *crypto_engine = NULL;
#endif

/**
 * Fetch OpenSSL error code and generate a string interpretation of it.
 *
 * @param[out] buf buffer to put string into
 * @param[in]  buflen size of buffer
 *
 * @returns buf
 **/
gchar *
z_ssl_get_error_str(gchar *buf, int buflen)
{
  const char *ls, *fs, *rs;
  unsigned long e, l, f, r;
  unsigned long new_error = 0;
  gint count = -1;

  do {
    e = new_error;
    new_error= ERR_get_error();
    ++count;
  } while (new_error);

  l = ERR_GET_LIB(e);
  f = ERR_GET_FUNC(e);
  r = ERR_GET_REASON(e);

  ls = ERR_lib_error_string(e);
  fs = ERR_func_error_string(e);
  rs = ERR_reason_error_string(e);

  if (count)
    g_snprintf(buf, buflen,
               "error:%08lX:%s:lib(%lu):%s:func(%lu):%s:reason(%lu), supressed %d messages",
               e, ls ? ls : "(null)", l, fs ? fs : "(null)", f, rs ? rs : "(null)", r, count);
  else
    g_snprintf(buf, buflen,
               "error:%08lX:%s:lib(%lu):%s:func(%lu):%s:reason(%lu)",
               e, ls ? ls : "(null)", l, fs ? fs : "(null)", f, rs ? rs : "(null)", r);
  return buf;
}

/**
 * Initialize OpenSSL library.
 **/
void
z_ssl_init()
{
  z_enter();

#if ZORPLIB_ENABLE_SSL_ENGINE
  ENGINE_load_builtin_engines();
  if (crypto_engine)
    {
      ENGINE *e;

      e = ENGINE_by_id(crypto_engine);
      if (!e)
        {
          e = ENGINE_by_id("dynamic");
          if (!e || 
              !ENGINE_ctrl_cmd_string(e, "SO_PATH", crypto_engine, 0) ||
              !ENGINE_ctrl_cmd_string(e, "LOAD", NULL, 0))
            {
              ENGINE_free(e);
              e = NULL;
              /*LOG
                This message indicates that an error occurred during the crypto engine load.
                Check your SSL crypto card installation.
               */
              z_log(NULL, CORE_ERROR, 1, "Error loading SSL engine module; crypto_engine='%s'", crypto_engine);
            }
        }
      if (e)
        {
          if (!ENGINE_set_default(e, ENGINE_METHOD_ALL))
            {
              /*LOG
                This message indicates that an error occurred during the crypto engine initialization.
                Check your SSL crypto card installation.
               */
              z_log(NULL, CORE_ERROR, 1, "Error initializing SSL crypto engine; crypto_engine='%s'", crypto_engine);
            }
          ENGINE_free(e);
        }
      else
        {
          /*LOG
            This message indicates that the given SSL crypto engine is not found. Check your
            SSL crypto card installation, and the crypto engines name.
           */
          z_log(NULL, CORE_ERROR, 1, "No such SSL crypto engine; crypto_engine='%s'", crypto_engine);
        }
    }
#endif

  z_return();
}

/**
 * Deinitialize OpenSSL library.
 **/
void
z_ssl_destroy(void)
{
}

/**
 * Lookup name in X.509 certificate verification store.
 *
 * @param store certificate verification store
 * @param type type
 * @param name name
 * @param obj object
 *
 * @todo FIXME-DOC: Document this once proper OpenSSL documentation is discovered.
 **/
static X509_OBJECT *
z_ssl_x509_store_lookup(X509_STORE *store, X509_LOOKUP_TYPE type, X509_NAME *name)
{
  z_enter();
  X509_STORE_CTX *store_ctx = X509_STORE_CTX_new();
  X509_STORE_CTX_init(store_ctx, store, NULL, NULL);
  X509_OBJECT *obj = X509_STORE_CTX_get_obj_by_subject(store_ctx, type, name);
  X509_STORE_CTX_free(store_ctx);
  z_return(obj);
}

int
z_ssl_verify_crl(int ok, 
                 X509 *xs,
                 X509_STORE_CTX *ctx,
                 X509_STORE *crl_store, 
                 const gchar *session_id)
{
  X509_OBJECT *obj;
  X509_NAME *subject, *issuer;
  X509_CRL *crl;
  char subject_name[512], issuer_name[512];

  z_enter(); 

  subject = X509_get_subject_name(xs);
  X509_NAME_oneline(subject, subject_name, sizeof(subject_name));

  issuer = X509_get_issuer_name(xs);
  X509_NAME_oneline(issuer, issuer_name, sizeof(issuer_name));
 
  obj = z_ssl_x509_store_lookup(crl_store, X509_LU_CRL, subject);
  crl = X509_OBJECT_get0_X509_CRL(obj);
  if (crl)
    {
      /*
       * Log information about CRL
       * (A little bit complicated because of ASN.1 and BIOs...)
       */
      BIO *bio;
      char *cp;
      EVP_PKEY *pkey;
      int n, i;

      bio = BIO_new(BIO_s_mem());
      BIO_printf(bio, "lastUpdate='");
      ASN1_UTCTIME_print(bio, X509_CRL_get0_lastUpdate(crl));
      BIO_printf(bio, "', nextUpdate='");
      ASN1_UTCTIME_print(bio, X509_CRL_get0_nextUpdate(crl));
      BIO_printf(bio, "'");
      n = BIO_pending(bio);

      cp = static_cast<char *>(alloca(n+1));
      n = BIO_read(bio, cp, n);
      cp[n] = 0;
      BIO_free(bio);

      /*LOG
        This message reports that the CA CRL verify starts for the given CA.
       */      
      z_log(session_id, CORE_DEBUG, 6, "Verifying CA CRL; issuer='%s', %s", subject_name, cp);

      pkey = X509_get_pubkey(xs);
      if (X509_CRL_verify(crl, pkey) <= 0)
        {
          /*LOG
            This message indicates an invalid Certificate Revocation List (CRL),
            because it is not signed by the CA it is said to belong to.
           */
          z_log(session_id, CORE_ERROR, 1, "Invalid signature on CRL; issuer='%s'", subject_name);
          X509_STORE_CTX_set_error(ctx, X509_V_ERR_CRL_SIGNATURE_FAILURE);
          X509_OBJECT_free(obj);
          EVP_PKEY_free(pkey);
          z_return(FALSE);
        }
      EVP_PKEY_free(pkey);

      i = X509_cmp_current_time(X509_CRL_get0_nextUpdate(crl));
      if (i == 0)
        {
          /*LOG
            This message indicates an invalid Certificate Revocation List (CRL),
            because it has an invalid nextUpdate field.
           */
          z_log(session_id, CORE_ERROR, 1, "CRL has invalid nextUpdate field; issuer='%s'", subject_name);

          X509_STORE_CTX_set_error(ctx, X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD);
          X509_OBJECT_free(obj);
          z_return(FALSE);
        }
      if (i < 0)
        {
          /*LOG
            This message indicates an invalid Certificate Revocation List (CRL),
            because it is expired.
           */
          z_log(session_id, CORE_ERROR, 1, "CRL is expired; issuer='%s'", subject_name);
          X509_STORE_CTX_set_error(ctx, X509_V_ERR_CRL_HAS_EXPIRED);
          X509_OBJECT_free(obj);
          z_return(FALSE);
        }
      X509_OBJECT_free(obj);
    }

  obj = z_ssl_x509_store_lookup(crl_store, X509_LU_CRL, issuer);
  crl = X509_OBJECT_get0_X509_CRL(obj);
  if (crl)
    {
      X509_REVOKED *revoked;
      int i, n;

      n = sk_X509_REVOKED_num(X509_CRL_get_REVOKED(crl));
      for (i = 0; i < n; i++)
        {
          revoked = sk_X509_REVOKED_value(X509_CRL_get_REVOKED(crl), i);
          if (ASN1_INTEGER_cmp(X509_REVOKED_get0_serialNumber(revoked), X509_get0_serialNumber(xs)) == 0)
            {
              /*LOG
                This message indicates that a certificate verification failed,
                because the issuing CA revoked it in its Certificate Revocation
                List.
               */
              z_log(session_id, CORE_ERROR, 1, "Certificate revoked by CRL; issuer='%s', serial=0x%lX",
                    issuer_name, ASN1_INTEGER_get(X509_REVOKED_get0_serialNumber(revoked)));
              X509_OBJECT_free(obj);
              z_return(FALSE);
            }
        }
      X509_OBJECT_free(obj);
    }
  z_return(ok);
}

static int
z_ssl_verify_callback(int ok, X509_STORE_CTX *ctx)
{
  ZSSLSession *verify_data;
  SSL *ssl;
  X509 *xs;
  char subject_name[512], issuer_name[512];
  int errnum;
  int errdepth;
  int forced_ok = FALSE;

  z_enter();
  ssl = (SSL *) X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
  verify_data = (ZSSLSession *) SSL_get_app_data(ssl);

  xs = X509_STORE_CTX_get_current_cert(ctx);
  errnum = X509_STORE_CTX_get_error(ctx);
  errdepth = X509_STORE_CTX_get_error_depth(ctx);

  X509_NAME_oneline(X509_get_subject_name(xs), subject_name, sizeof(subject_name));
  X509_NAME_oneline(X509_get_issuer_name(xs),  issuer_name, sizeof(issuer_name));

  /*LOG
    This message indicates that the given certificate is being checked against
    validity.
   */
  z_log(verify_data->session_id, CORE_DEBUG, 6, "Verifying certificate; depth='%d', subject='%s', issuer='%s'", 
        errdepth, subject_name, issuer_name);

  if ((verify_data->verify_type == Z_SSL_VERIFY_REQUIRED_UNTRUSTED || 
       verify_data->verify_type == Z_SSL_VERIFY_OPTIONAL) &&
      (errnum == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT || 
       errnum == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN || 
       errnum == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY || 
       errnum == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT || 
       errnum == X509_V_ERR_CERT_UNTRUSTED || 
       errnum == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE))
    {
      /*LOG
        This message indicates that certificate verification failed, but
        it is ignored since the administrator requested optional certificate
        verification.
       */
      z_log(verify_data->session_id, CORE_ERROR, 4, "Untrusted certificate, ignoring because verification is not mandatory; subject='%s', issuer='%s'", 
            subject_name, issuer_name);
      ok = TRUE;
      forced_ok = TRUE;
    }

  if (ok && verify_data->crl_store)
    {
      ok = z_ssl_verify_crl(ok, xs, ctx, verify_data->crl_store, verify_data->session_id);
      if (!ok)
        {
          errnum = X509_STORE_CTX_get_error(ctx);
          /*LOG
            This message indicates that a certificate verification failed,
            because the issuing CA revoked it in its Certificate Revocation
            List.
           */
          z_log(verify_data->session_id, CORE_ERROR, 1, "Certificate is revoked; subject='%s', issuer='%s'", 
                subject_name, issuer_name);
        }
    }

  if (ok && (verify_data->verify_depth != -1) &&
      (errdepth > verify_data->verify_depth))
    {
      /*LOG
        Certificate was verified successfully, but the length of the
        verification chain is over configured limits.
       */
      z_log(verify_data->session_id, CORE_ERROR, 1, "Certificate chain is too long; subject='%s', issuer='%s' depth='%d' maxdepth='%d'", subject_name, issuer_name, errdepth, verify_data->verify_depth);
      errnum = X509_V_ERR_CERT_CHAIN_TOO_LONG;
      ok = FALSE;
    }

  if (!ok)
    /*LOG
      This message indicates that certificate could not be verified, and
      the certificate is treated as invalid.
     */
    z_log(verify_data->session_id, CORE_ERROR, 1, "Certificate verification error; subject='%s', issuer='%s', errcode='%d', error='%s'", subject_name, issuer_name, errnum, X509_verify_cert_error_string(errnum));
  z_return(ok || forced_ok);
}

X509_STORE *
z_ssl_crl_store_create(gchar *crl_path)
{
  X509_STORE *store;
  X509_LOOKUP *lookup;

  z_enter();        
  if ((store = X509_STORE_new()) == NULL)
    z_return(NULL);
  if (crl_path != NULL)
    {
      if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir())) == NULL)
        {
          X509_STORE_free(store);
          z_return(NULL);
        }
      X509_LOOKUP_add_dir(lookup, crl_path, X509_FILETYPE_PEM);
    }
  z_return(store);
}

typedef struct _ZSSLCADirectory
{
  time_t modtime;
  STACK_OF(X509_NAME) *contents;
} ZSSLCADirectory;

static int 
z_ssl_X509_name_cmp(const X509_NAME * const *a, const X509_NAME * const *b)
{
  return(X509_NAME_cmp(*a, *b));
}

static STACK_OF(X509_NAME) *
z_ssl_dup_sk_x509_name(STACK_OF(X509_NAME) *old)
{
  STACK_OF(X509_NAME) *sk;
  int i;

  z_enter();
  sk = sk_X509_NAME_new_null();
  for (i = 0; i < sk_X509_NAME_num(old); i++)
    {
      X509_NAME *xn;

      xn = sk_X509_NAME_value(old, i);
      sk_X509_NAME_push(sk, X509_NAME_dup(xn));
    }
  z_return(sk);
}

gboolean
z_ssl_set_trusted_ca_list(SSL_CTX *ctx, gchar *ca_path)
{
  ZSSLCADirectory *ca_dir = NULL;
  static GHashTable *ca_dir_hash = NULL;
  G_LOCK_DEFINE_STATIC(lock);
  STACK_OF(X509_NAME) *ca_file = NULL;
  const gchar *direntname;
  struct stat ca_stat;
  GDir *dir;

  z_enter();
  G_LOCK(lock);
  if (ca_dir_hash == NULL)
    {
      ca_dir_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }
  else
    {
      gpointer orig_key;
      gpointer value;

      if (g_hash_table_lookup_extended(ca_dir_hash, ca_path, &orig_key, &value))
        {
          ca_dir = (ZSSLCADirectory *) value;
          if (stat(ca_path, &ca_stat) >= 0 &&
              ca_dir->modtime == ca_stat.st_mtime)
            {
              SSL_CTX_set_client_CA_list(ctx, z_ssl_dup_sk_x509_name(ca_dir->contents));
              G_UNLOCK(lock);
              z_return(TRUE);
            }
          g_hash_table_remove(ca_dir_hash, orig_key);
          g_free(orig_key);
          sk_X509_NAME_pop_free(ca_dir->contents, X509_NAME_free);
          g_free(ca_dir);
        }
    }

  if (stat(ca_path, &ca_stat) < 0)
    {
      G_UNLOCK(lock);
      z_return(FALSE);
    }
  ca_dir = g_new0(ZSSLCADirectory, 1);
  ca_dir->modtime = ca_stat.st_mtime;
  ca_dir->contents = sk_X509_NAME_new(z_ssl_X509_name_cmp);

  dir = g_dir_open(ca_path,0,NULL);
  if (dir)
    {
      while ((direntname = g_dir_read_name(dir)) != NULL)
        {
          char file_name[256];
          int i;

          g_snprintf(file_name, sizeof(file_name), "%s/%s", ca_path, direntname);
          ca_file = SSL_load_client_CA_file(file_name);
          if (!ca_file)
            {
              /*LOG
                This message indicates that an error occurred during loading client CA certificates
                from the given file. It is likely that the file is not readable or it is in a wrong format.
               */
              z_log(NULL, CORE_ERROR, 4, "Error loading CA certificate bundle, skipping; filename='%s'", file_name);
              continue;
            }

          for (i = 0; ca_file != NULL && i < sk_X509_NAME_num(ca_file); i++)
            {
              if (sk_X509_NAME_find(ca_dir->contents, sk_X509_NAME_value(ca_file, i)) < 0)
                sk_X509_NAME_push(ca_dir->contents, sk_X509_NAME_value(ca_file, i));
              else
                X509_NAME_free(sk_X509_NAME_value(ca_file, i));
            }
          sk_X509_NAME_free(ca_file);
        }
    }
  g_hash_table_insert(ca_dir_hash, g_strdup(ca_path), ca_dir);
  SSL_CTX_set_client_CA_list(ctx, z_ssl_dup_sk_x509_name(ca_dir->contents));
  g_dir_close(dir);
  G_UNLOCK(lock);
  z_return(TRUE);
}

bool z_ssl_X509_is_self_signed(X509* cert)
{
  return X509_get_extension_flags(cert) & EXFLAG_SS;
}

enum SSLContextType
{
  WEAK,
  HARDENED
};

static SSL_CTX *
z_ssl_create_ctx(const char *session_id, int mode, SSLContextType ctx_type)
{
  SSL_CTX *ctx = nullptr;
  char buf[128];

  if (mode == Z_SSL_MODE_CLIENT)
      ctx = SSL_CTX_new(TLS_client_method());
  else
      ctx = SSL_CTX_new(TLS_server_method());

  if (!ctx)
    {
      /*LOG
        This message indicates that an SSL_CTX couldn't be allocated, it
        usually means that Zorp has not enough memory. You might want
        to check your ulimit settings and/or the available physical/virtual
        RAM in your firewall host.
       */
      z_log(
        session_id, CORE_ERROR, 3,
        "Error allocating new SSL_CTX; error='%s'", z_ssl_get_error_str(buf, sizeof(buf))
      );
      return nullptr;
    }

  if (mode == Z_SSL_MODE_SERVER)
    SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

  SSL_CTX_set_options(ctx, SSL_OP_ALL);

  int set_min = 0;
  int set_max = 0;
  switch (ctx_type)
    {
      case HARDENED:
        set_min = SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        set_max = SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
        if (!set_min || !set_max)
          {
            z_log(
              session_id, CORE_ERROR, 3,
              "Error setting SSL_CTX protocol version; error='%s'", z_ssl_get_error_str(buf, sizeof(buf))
            );
            SSL_CTX_free(ctx);
            return nullptr;
          }

        SSL_CTX_set_cipher_list(ctx, "EECDH+AES256:EECDH+AES128:!SHA1");

        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
        SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
        if (!SSL_CTX_set_num_tickets(ctx, 0))
          {
            z_log(
              session_id, CORE_ERROR, 3,
              "Error setting SSL_CTX number of TLSv1.3 tickets to zero; error='%s'",
              z_ssl_get_error_str(buf, sizeof(buf))
            );
            SSL_CTX_free(ctx);
            return nullptr;
          }
        break;

      case WEAK:
        break;
    }

  return ctx;
}

/* FIXME: SSL context cache */
#ifndef G_OS_WIN32

static int
z_ssl_password(char */* buf */, int /* size */, int /* rwflag */, void * /* userdata */)
{
  z_log(NULL, CORE_ERROR, 1, "Password protected key file detected;");
  return -1;
}

static gboolean
z_ssl_load_privkey_and_cert(const char *session_id, SSL_CTX *ctx, gchar *key_file, gchar *cert_file)
{
  char buf[128];

  z_enter();
  if (key_file && key_file[0])
    {
      SSL_CTX_set_default_passwd_cb(ctx, z_ssl_password);
      if (!SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM))
        {
          /*LOG
            This message indicates that the given private key could not be 
            loaded. Either the private key is not in PEM format, or 
            the key data was encrypted.
           */
          z_log(session_id, CORE_ERROR, 3, "Error loading private key; keyfile='%s', error='%s'", key_file, z_ssl_get_error_str(buf, sizeof(buf)));
          z_return(FALSE);
        }
      if (!SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM))
        {
          /*LOG
            This message indicates that the given certificate file could not
            be loaded. It might not be in PEM format or otherwise corrupted.
           */
          z_log(session_id, CORE_ERROR, 3, "Error loading certificate file; keyfile='%s', certfile='%s', error='%s'", key_file, cert_file, z_ssl_get_error_str(buf, sizeof(buf)));
          z_return(FALSE);        
        }
      if (!SSL_CTX_check_private_key(ctx))
        {
          /*LOG
            This message indicates that the private key and corresponding
            certificate do not match.
           */
          z_log(session_id, CORE_ERROR, 3, "Certificate and private key mismatch; keyfile='%s', certfile='%s', error='%s'", key_file, cert_file, z_ssl_get_error_str(buf, sizeof(buf)));
          z_return(FALSE);
        }
      /*LOG
        This message reports that the given private key- and
        certificate files were successfully loaded.
       */
      z_log(session_id, CORE_DEBUG, 6, "Certificate file successfully loaded; keyfile='%s', certfile='%s'", key_file, cert_file);
    }
  z_return(TRUE);
}

static gboolean
z_ssl_set_privkey_and_cert(const char *session_id, SSL_CTX *ctx, GString *key_pem, GString *cert_pem)
{
  char buf[128];

  z_enter();
  if (key_pem && key_pem->len)
    {
      EVP_PKEY *epk;
      RSA *rsa;
      BIO *bio;
      X509 *x509;

      bio = BIO_new_mem_buf(key_pem->str, key_pem->len);
      if (!bio)
        {
          z_log(session_id, CORE_ERROR, 3, "Cannot create BIO for private key;");
          z_return(FALSE);
        }

      rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
      BIO_free(bio);
      if (!rsa)
        {
          z_log(session_id, CORE_ERROR, 3, "Cannot parse rsa private key;");
          z_return(FALSE);
        }

      epk = EVP_PKEY_new();
      EVP_PKEY_assign_RSA(epk, rsa);
      SSL_CTX_set_default_passwd_cb(ctx, z_ssl_password);
      if (!SSL_CTX_use_PrivateKey(ctx, epk))
        {
          /*LOG
            This message indicates that the given private key could not be 
            loaded. Either the private key is not in PEM format, or 
            the key data was encrypted.
            */
          z_log(session_id, CORE_ERROR, 3, "Error loading private key; error='%s'", z_ssl_get_error_str(buf, sizeof(buf)));
          EVP_PKEY_free(epk);
          z_return(FALSE);
        }
      EVP_PKEY_free(epk);

      bio = BIO_new_mem_buf(cert_pem->str, cert_pem->len);
      if (!bio)
        {
          z_log(session_id, CORE_ERROR, 3, "Cannot create BIO for certificate;");
          z_return(FALSE);
        }

      x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
      BIO_free(bio);
      if (!SSL_CTX_use_certificate(ctx, x509))
        {
          /*LOG
            This message indicates that the given certificate file could not
            be loaded. It might not be in PEM format or otherwise corrupted.
            */
          z_log(session_id, CORE_ERROR, 3, "Error loading certificate; error='%s'", z_ssl_get_error_str(buf, sizeof(buf)));
          X509_free(x509);
          z_return(FALSE);        
        }
      X509_free(x509);

      if (!SSL_CTX_check_private_key(ctx))
        {
          /*LOG
            This message indicates that the private key and corresponding
            certificate do not match.
            */
          z_log(session_id, CORE_ERROR, 3, "Certificate and private key mismatch; error='%s'", z_ssl_get_error_str(buf, sizeof(buf)));
          z_return(FALSE);
        }
      /*LOG
        This message reports that the given private key- and
        certificate files were successfully loaded.
        */
      z_log(session_id, CORE_DEBUG, 6, "Certificate successfully loaded;");
    }
  z_return(TRUE);
}

static gboolean
z_ssl_load_ca_list(const char *session_id, SSL_CTX *ctx, int mode, gchar *ca_dir, gchar *crl_dir, X509_STORE **crl_store)
{
  z_enter();
  if (ca_dir && ca_dir[0])
    {
      if (mode == Z_SSL_MODE_SERVER)
        {
          if (!z_ssl_set_trusted_ca_list(ctx, ca_dir))
            {
              /*LOG
                This message indicates that loading the trustable CA certificates
                failed. The TLS peer is told which CAs we trust to ease the
                selection of certificates. This could mean that one or more
                of your CA certificates are invalid.
               */
              z_log(session_id, CORE_ERROR, 3, "Error loading trusted CA list; cadir='%s'", ca_dir);
              z_return(FALSE);
            }
        }
      if (access(ca_dir, R_OK | X_OK) < 0)
        {
          z_log(session_id, CORE_ERROR, 3, "Insufficient permissions to CA directory; cadir='%s', error='%s'", ca_dir, g_strerror(errno));
          z_return(FALSE);
        }
      if (!SSL_CTX_load_verify_locations(ctx, NULL, ca_dir))
        {
          /*LOG
            This message indicates that setting the trusted CA directory for
            verification failed.
           */
          z_log(session_id, CORE_ERROR, 3, "Cannot add trusted CA directory as verify location; cadir='%s'", ca_dir);
          z_return(FALSE);
        }
      if (crl_dir && crl_dir[0])
        {
          if (access(crl_dir, R_OK | X_OK) < 0)
            {
              z_log(session_id, CORE_ERROR, 3, "Insufficient permissions to CRL directory; crldir='%s', error='%s'", crl_dir, g_strerror(errno));
              z_return(FALSE); 
            }
          *crl_store = z_ssl_crl_store_create(crl_dir);
        }
      else
        {
          /*LOG
            This message reports that CRLs are not in use for certificate
            verification.
           */
          z_log(session_id, CORE_DEBUG, 6, "Certificate Revocation Lists not available;");
        }
    }
  z_return(TRUE);
}

static ZSSLSession *
z_ssl_session_new_from_context(const char *session_id, SSL_CTX *ctx, int verify_depth, int verify_type, X509_STORE *crl_store)
{
  ZSSLSession *self = NULL;
  SSL *session;
  int verify_mode = 0;

  z_enter();
  session = SSL_new(ctx);
  if (!session)
    {
      /*LOG
        This message indicates that creating the SSL session structure failed.
        It's usually caused by out-of-memory conditions.
       */
      z_log(session_id, CORE_ERROR, 3, "Error creating SSL session from SSL_CTX;");
      if (crl_store)
        X509_STORE_free(crl_store);
      z_return(NULL);
    }

  self = g_new0(ZSSLSession, 1);
  self->ref_cnt = 1;
  self->ssl = session;  
  self->session_id = session_id;
  self->verify_type = verify_type;
  self->verify_depth = verify_depth;
  self->crl_store = crl_store;
  SSL_set_app_data(session, self);

  if (verify_type == Z_SSL_VERIFY_OPTIONAL || 
      verify_type == Z_SSL_VERIFY_REQUIRED_UNTRUSTED)
    verify_mode = SSL_VERIFY_PEER;

  if (verify_type == Z_SSL_VERIFY_REQUIRED_TRUSTED)
    verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

  if (verify_mode != 0)
    SSL_set_verify(session, verify_mode, z_ssl_verify_callback);

  z_return(self);
}

ZSSLSession *
z_ssl_session_new_inline(const char *session_id, 
                         int mode,
                         GString *key_pem, 
                         GString *cert_pem, 
                         gchar *ca_dir, 
                         gchar *crl_dir, 
                         int verify_depth,
                         int verify_type)
{
  ZSSLSession *self;
  SSL_CTX *ctx;
  X509_STORE *crl_store = NULL;

  z_enter();
  ctx = z_ssl_create_ctx(session_id, mode, WEAK);
  if (!ctx)
    z_return(NULL);

  if (!z_ssl_set_privkey_and_cert(session_id, ctx, key_pem, cert_pem) ||
      !z_ssl_load_ca_list(session_id, ctx, mode, ca_dir, crl_dir, &crl_store))
    {
      SSL_CTX_free(ctx);
      z_return(NULL);
    }
  self = z_ssl_session_new_from_context(session_id, ctx, verify_depth, verify_type, crl_store);
  SSL_CTX_free(ctx);
  z_return(self);
}

ZSSLSession *
z_ssl_session_new(const char *session_id, 
                  int mode,
                  gchar *key_file, 
                  gchar *cert_file, 
                  gchar *ca_dir, 
                  gchar *crl_dir, 
                  int verify_depth,
                  int verify_type)
{
  ZSSLSession *self;
  SSL_CTX *ctx;
  X509_STORE *crl_store = NULL;

  z_enter();
  ctx = z_ssl_create_ctx(session_id, mode, HARDENED);
  if (!ctx)
    z_return(NULL);

  if (!z_ssl_load_privkey_and_cert(session_id, ctx, key_file, cert_file) ||
      !z_ssl_load_ca_list(session_id, ctx, mode, ca_dir, crl_dir, &crl_store))
    {
      SSL_CTX_free(ctx);
      z_return(NULL);
    }
  self = z_ssl_session_new_from_context(session_id, ctx, verify_depth, verify_type, crl_store);
  SSL_CTX_free(ctx);
  z_return(self);
}
#else

ZSSLSession *
z_ssl_session_new(const char *session_id, 
                  int mode,
                  X509_STORE *store, 
                  int verify_depth,
                  int verify_type)

{
  ZSSLSession *self;
  SSL_CTX *ctx;
  SSL *session;
  int verify_mode = 0;
  char buf[128];

  z_enter();
  ctx = z_ssl_create_ctx(session_id, mode, HARDENED);
  if (!ctx)
    z_return(NULL);

  if (store)
    SSL_CTX_set_cert_store(ctx, store);

  session = SSL_new(ctx);
  SSL_CTX_free(ctx);
  if (!session)
    {
      /*LOG
        This message indicates that creating the SSL session structure failed.
        It's usually caused by out-of-memory conditions.
       */
      z_log(session_id, CORE_ERROR, 3, "Error creating SSL session from SSL_CTX;");
      if (store)
        X509_STORE_free(store);
      z_return(NULL);
    }

  /* FIXME: CRL handling */  
  self = g_new0(ZSSLSession, 1);
  self->ref_cnt = 1;
  self->ssl = session;  
  self->session_id = session_id;
  self->verify_type = verify_type;
  self->verify_depth = verify_depth;
  SSL_set_app_data(session, self);

  if (verify_type == Z_SSL_VERIFY_OPTIONAL || 
      verify_type == Z_SSL_VERIFY_REQUIRED_UNTRUSTED)
    verify_mode = SSL_VERIFY_PEER;
  if (verify_type == Z_SSL_VERIFY_REQUIRED_TRUSTED)
    verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

  if (verify_mode != 0)
    SSL_set_verify(session, verify_mode, z_ssl_verify_callback);
  z_return(self);

}
#endif

ZSSLSession *
z_ssl_session_new_ssl(SSL *ssl)
{
  if (SSL_up_ref(ssl) == 0)
    return NULL;

  ZSSLSession *self = g_new0(ZSSLSession, 1);
  self->ref_cnt = 1;
  self->ssl = ssl;
  return self;
}

static void
z_ssl_session_free(ZSSLSession *self)
{
  z_enter();
  /* free verify_data */
  SSL_free(self->ssl);
  if (self->crl_store)
    X509_STORE_free(self->crl_store);
  g_free(self);
  z_return();
}

ZSSLSession *
z_ssl_session_ref(ZSSLSession *self)
{
  self->ref_cnt++;
  return self;
}

void
z_ssl_session_unref(ZSSLSession *self)
{
  if (self && --self->ref_cnt == 0)
    {
      z_ssl_session_free(self);
    }
}

/* SSL BIO functions */

int
z_stream_bio_write(BIO *bio, const char *buf, int buflen)
{
  ZStreamBio *self = static_cast<ZStreamBio *>(BIO_get_data(bio));
  int rc = -1;
  GIOStatus ret;
  gsize write_size;

  z_enter();
  if (buf != NULL)
    {
      ret = z_stream_write(self->stream, buf, buflen, &write_size, NULL);
      rc = (int)write_size;
      BIO_clear_retry_flags(bio);
      if (ret == G_IO_STATUS_AGAIN)
        {
          BIO_set_retry_write(bio);
          z_return(-1);
        }
      if (ret != G_IO_STATUS_NORMAL)
        z_return(-1);
    }
  z_return(rc);
}

int
z_stream_bio_read(BIO *bio, char *buf, int buflen)
{
  ZStreamBio *self = static_cast<ZStreamBio *>(BIO_get_data(bio));
  int rc = -1;
  GIOStatus ret;
  gsize read_size;

  z_enter();
  if (buf != NULL)
    {
      ret = z_stream_read(self->stream, buf, buflen, &read_size, NULL);
      rc = (int)read_size;
      BIO_clear_retry_flags(bio);
      if (ret == G_IO_STATUS_AGAIN)
        {
          BIO_set_retry_read(bio);
          z_return(-1);
        }
      if (ret == G_IO_STATUS_EOF)
        z_return(0);
      if (ret != G_IO_STATUS_NORMAL)
        z_return(-1);
    }
  z_return(rc);
}

int
z_stream_bio_puts(BIO *bio, const char *str)
{
  int n, ret;

  z_enter();
  n = strlen(str);
  ret = z_stream_bio_write(bio, str, n);
  z_return(ret);
}

long
z_stream_bio_ctrl(BIO *bio, int cmd, long num, void */* ptr */)
{
  long ret = 1;

  z_enter();
  switch (cmd)
    {
    case BIO_CTRL_GET_CLOSE:
      ret = BIO_get_shutdown(bio);
      break;

    case BIO_CTRL_SET_CLOSE:
      BIO_set_shutdown(bio, static_cast<int>(num));
      break;

    case BIO_CTRL_DUP:
    case BIO_CTRL_FLUSH:
      ret = 1;
      break;

    case BIO_CTRL_RESET:
    case BIO_C_FILE_SEEK:
    case BIO_C_FILE_TELL:
    case BIO_CTRL_INFO:
    case BIO_C_SET_FD:
    case BIO_C_GET_FD:
    case BIO_CTRL_PENDING:
    case BIO_CTRL_WPENDING:
    default:
      ret = 0;
      break;
    }
  z_return(ret);
}

int
z_stream_bio_create(BIO *bio)
{
  z_enter();
  BIO_set_init(bio, 1);
  BIO_set_flags(bio, 0);
  BIO_set_data(bio, nullptr);
  BIO_set_shutdown(bio, 0);
  z_return(1);
}

int
z_stream_bio_destroy(BIO *bio)
{
  ZStreamBio *self = static_cast<ZStreamBio *>(BIO_get_data(bio));

  z_enter();
  if (self == NULL)
    z_return(0);
  if (BIO_get_shutdown(self->super))
    {
      z_stream_shutdown(self->stream, 2, NULL);
      BIO_set_init(bio, 0);
      BIO_set_flags(bio, 0);
    }

  z_stream_unref(self->stream);
  g_free(self);

  z_return(1);
}

std::shared_ptr<BIO_METHOD> z_ssl_bio_method;

ZStreamBio *
z_ssl_bio_new(ZStream *stream)
{
  z_enter();

  if (!z_ssl_bio_method)
    {
      std::shared_ptr<BIO_METHOD> bio_method = std::shared_ptr<BIO_METHOD>(
        BIO_meth_new(BIO_TYPE_DGRAM, "Zorp Stream BIO"),
        BIO_meth_free
      );
      if (!bio_method
          || !BIO_meth_set_write(bio_method.get(), z_stream_bio_write)
          || !BIO_meth_set_read(bio_method.get(), z_stream_bio_read)
          || !BIO_meth_set_puts(bio_method.get(), z_stream_bio_puts)
          || !BIO_meth_set_ctrl(bio_method.get(), z_stream_bio_ctrl)
          || !BIO_meth_set_create(bio_method.get(), z_stream_bio_create)
          || !BIO_meth_set_destroy(bio_method.get(), z_stream_bio_destroy))
        return nullptr;
      z_ssl_bio_method = bio_method;
    }

  ZStreamBio *self = g_new0(ZStreamBio, 1);

  self->stream = z_stream_ref(stream);

  /**
   *
   * SSL_set_bio() will transfers the ownership to ssl and when the rbio and wbio parameters are the same and the rbio
   * is not the same as the previously set value then one reference is consumed.
   *
   **/
  self->super = BIO_new(z_ssl_bio_method.get());

  BIO_set_data(self->super, self);

  z_return(self);
}
