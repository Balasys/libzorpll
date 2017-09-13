/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_ZOBJECT_H_INCLUDED
#define ZORP_ZOBJECT_H_INCLUDED

/* this might need to be moved to libzorpll */

#include <zorpll/zorplib.h>
#include <zorpll/misc.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _ZClass ZClass;
typedef struct _ZObject ZObject;

/**
 * struct type to hold ZObject virtual methods.
 **/
typedef struct _ZObjectFuncs
{
  gint method_count;
  void (*free_fn)(ZObject *self);
} ZObjectFuncs;

/**
 * Base class.
 *
 * Not all classes in zorp-lib use it.
 **/
struct _ZObject
{
  ZRefCount ref_cnt;
  ZClass *isa;
};

/**
 * The metaclass of the object system.
 **/
struct _ZClass
{
  ZObject super;
  gboolean funcs_resolved; /**< indicates whether method inheritance was already performed */
  struct _ZClass *super_class;
  const gchar *name;
  gsize size;
  ZObjectFuncs *funcs; 
};

LIBZORPLL_EXTERN ZClass ZClass__class;

LIBZORPLL_EXTERN ZClass ZObject__class;

typedef ZClass ZInterface;

#define Z_OBJECT_HEADER   { Z_REFCOUNT_INIT, NULL }
#define Z_CLASS_HEADER    Z_OBJECT_HEADER, 0

#define Z_CLASS(class_)       (&class_##__class)

#define Z_CLASS_DEF(klass_, super_class, class_methods) \
  ZClass klass_##__class =                              \
    {                                                   \
      Z_CLASS_HEADER,                                   \
      Z_CLASS(super_class),                             \
      #klass_,                                          \
      sizeof(klass_),                                   \
      (ZObjectFuncs*) &class_methods,                   \
    }

#define Z_CAST(inst, class_) ((class_ *) z_object_check_compatible((ZObject *) inst, Z_CLASS(class_)))

#define Z_FUNCS(inst, class_) ((class_##Funcs *) (z_object_check_compatible((ZObject *) inst, Z_CLASS(class_))->isa->funcs))
#define Z_FUNCS_CALL(inst, class_, func_name, ...) Z_FUNCS(inst, class_)->func_name(inst, ## __VA_ARGS__)
#define Z_SUPER(inst, class_) ((class_##Funcs *) (z_object_check_compatible((ZObject *) inst, Z_CLASS(class_))->isa->super_class->funcs))
#define Z_FUNCS_COUNT(class_) ((sizeof(class_##Funcs)-sizeof(gint))/sizeof(void (*)(void)))
#define Z_NEW(class_)         (class_ *) z_object_new(Z_CLASS(class_))
#define Z_NEW_COMPAT(class_, compat) (compat *) z_object_new_compatible(class_, Z_CLASS(compat))

ZObject *z_object_new(ZClass *class_);
ZObject *z_object_new_compatible(ZClass *class_, ZClass *compat);
gboolean z_object_is_compatible(ZObject *self, ZClass *class_);
gboolean z_object_is_subclass(ZClass *class_, ZClass *subclass);
gboolean z_object_is_instance(ZObject *self, ZClass *class_);

#if ZORPLIB_ENABLE_DEBUG

/**
 * Check if self is compatible with class, e.g.\ whether it's derived from class.
 *
 * @param[in] self object
 * @param[in] class_ class
 *
 * Debug version. Asserts that the above assumption is true.
 *
 * @returns self
 **/
static inline ZObject *
z_object_check_compatible(ZObject *self, ZClass *class_)
{
  g_assert(!self || z_object_is_compatible(self, class_));
  return self;
}

#else

/**
 * Check if self is compatible with class, e.g.\ whether it's derived from class.
 *
 * @param[in] self object
 * @param     class_ class (unused)
 *
 * Non-debug version, doesn't really do anything but return self.
 *
 * @returns self
 **/
static inline ZObject *
z_object_check_compatible(ZObject *self, ZClass *class_ G_GNUC_UNUSED)
{
  return self;
}

#endif

/* function declaration for virtual functions */

void z_object_free_method(ZObject *s);

/**
 * Increment the reference count of self and return a reference.
 *
 * @param[in,out] self ZObject instance
 *
 * @returns self
 **/
static inline ZObject *
z_object_ref(ZObject *self)
{
  if (self)
    z_refcount_inc(&self->ref_cnt);
  return self;
}

/**
 * Decrement the reference count of self and free it if the reference count
 * goes down to zero.
 *
 * @param[in] self ZObject instance
 **/
static inline void
z_object_unref(ZObject *self)
{
  if (self && z_refcount_dec(&self->ref_cnt))
    {
      Z_FUNCS(self, ZObject)->free_fn(self);
      g_free(self);
    }
}

#ifdef __cplusplus
}
#endif

#endif
