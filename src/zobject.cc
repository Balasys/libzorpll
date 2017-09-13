/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/zobject.h>
#include <zorpll/log.h>

/**
 * struct type to hold ZObject virtual methods.
 **/
typedef struct _ZClassFuncs
{ 
  gint method_count;
  void (*methods[1])(void);
} ZClassFuncs;

/**
 * This function is the free method of the ZObject class.
 *
 * @param  s ZObject instance
 *
 * This function currently does nothing as ZObject has no real data structure.
 **/
void
z_object_free_method(ZObject *s G_GNUC_UNUSED)
{
  /* empty */
}

/**
 * Dummy ZClass class descriptor.
 **/
#ifdef G_OS_WIN32
  LIBZORPLL_EXTERN
#endif
ZClass ZClass__class =
{
  Z_CLASS_HEADER,
  NULL,
  "ZClass",
  sizeof(ZClass),
  NULL
};

/**
 * ZObject virtual methods.
 **/
static ZObjectFuncs z_object_funcs = 
{
  Z_FUNCS_COUNT(ZObject),
  z_object_free_method
};

/**
 * Descriptor of ZObject, the base (or root) class.
 **/
#ifdef G_OS_WIN32
  LIBZORPLL_EXTERN
#endif
ZClass ZObject__class =
{
  Z_CLASS_HEADER,
  NULL,
  "ZObject",
  sizeof(ZObject),
  &z_object_funcs
};

/**
 * This function checks whether subclass is derived from class.
 *
 * @param[in] klass parent class
 * @param[in] subclass child class
 **/
gboolean
z_object_is_subclass(ZClass *klass, ZClass *subclass)
{
  ZClass *p;
  
  p = subclass;
  while (p && p != klass)
    p = p->super_class;
  return !!p;
}

/**
 * This function checks whether self is compatible of class, e.g.\ whether
 * it is derived from class.
 *
 * @param[in] self ZObject instance
 * @param[in] klass class descriptor
 **/
gboolean
z_object_is_compatible(ZObject *self, ZClass *klass)
{
  if (!self)
    return FALSE;
  else
    return z_object_is_subclass(klass, self->isa);
}

/**
 * This function returns whether self is an instance of class.
 *
 * @param[in] self ZObject instance
 * @param[in] klass class descriptor
 **/
gboolean
z_object_is_instance(ZObject *self, ZClass *klass)
{
  if (!self)
    return FALSE;
  else
    return self->isa == klass;
}

/**
 * Resolve NULL methods to be used from parent classes.
 *
 * @param[in] klass class descriptor
 *
 * This function is called once for each class instantiated. It resolves
 * NULL methods to be used from parent classes.
 **/
static void
z_object_resolve_funcs(ZClass *klass)
{
  gint i;
  
  if (klass->funcs_resolved)
    return;
  if (klass->super_class)
    {
      z_object_resolve_funcs(klass->super_class);
      for (i = 0; i < klass->super_class->funcs->method_count; i++)
        {
          /* inherit from parent */
          if (((ZClassFuncs *) klass->funcs)->methods[i] == NULL)
            ((ZClassFuncs *) klass->funcs)->methods[i] = ((ZClassFuncs *) klass->super_class->funcs)->methods[i];
        }
    }
  klass->funcs_resolved = TRUE;
}

/**
 * Creates a new instance of class.
 *
 * @param[in] klass class descriptor
 *
 * This function takes a class descriptor and creates an instance of class
 * by allocating an appropriate memory block, initializing the reference
 * counter and ZObject specific fields.
 *
 * @returns the new object
 **/
ZObject *
z_object_new(ZClass *klass)
{
  ZObject *inst;
  
  if (klass->super.isa == NULL)
    klass->super.isa = &ZClass__class;
  
  if (!klass->funcs_resolved)
    z_object_resolve_funcs(klass);
  
  inst = static_cast<ZObject *>(g_malloc0(klass->size));
  inst->isa = klass;
  z_refcount_set(&inst->ref_cnt, 1);
  return inst;
}

/**
 * This function checks whether class is compatible with compat, and if it
 * is, it returns a new instance of class.
 *
 * @param[in] klass class decriptor
 * @param[in] compat class descriptor
 **/
ZObject *
z_object_new_compatible(ZClass *klass, ZClass *compat)
{
  if (z_object_is_subclass(compat, klass))
    {
      return z_object_new(klass);
    }
  else
    {
      g_assert(0 && "Requested class is not compatible");
      return NULL;
    }
}
