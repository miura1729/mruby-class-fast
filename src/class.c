#include <mruby.h>
#include <mruby/class.h>
#include <mruby/proc.h>
#include <mruby/irep.h>
#include <mruby/gc.h>
#include <mruby/opcode.h>

static mrb_callinfo*
cipush(mrb_state *mrb)
{
  struct mrb_context *c = mrb->c;
  mrb_callinfo *ci = c->ci;

  int eidx = ci->eidx;
  int ridx = ci->ridx;

  if (ci + 1 == c->ciend) {
    ptrdiff_t size = ci - c->cibase;

    c->cibase = (mrb_callinfo *)mrb_realloc(mrb, c->cibase, sizeof(mrb_callinfo)*size*2);
    c->ci = c->cibase + size;
    c->ciend = c->cibase + size * 2;
  }
  ci = ++c->ci;
  ci->eidx = eidx;
  ci->ridx = ridx;
  ci->env = 0;
  ci->pc = 0;
  ci->err = 0;
  ci->proc = 0;

  return ci;
}

static void
patch_irep(mrb_state *mrb, struct RProc *m) 
{
  /* patch initialize method */
  unsigned int i;
  mrb_irep *pirep = m->body.irep;
  mrb_code *piseq = pirep->iseq;

  if (GETARG_A(piseq[pirep->ilen - 1]) == 0) {
    return;
  }

  for (i = 0; i < pirep->ilen; i++) {
    if (GET_OPCODE(piseq[i]) == OP_RETURN && 
	(piseq[i] & ((1 << 23) - 1)) != piseq[i]) {
      pirep->iseq = (mrb_code *)mrb_malloc(mrb, pirep->ilen *  sizeof(mrb_code));
      for (unsigned int j = 0; j < pirep->ilen; j++) {
	pirep->iseq[j] = piseq[j];
      }
      if (!(pirep->flags & MRB_ISEQ_NO_FREE)) {
	mrb_free(mrb, piseq);
      }
      piseq = pirep->iseq;
      break;
    }
  }

  for (i = 0; i < pirep->ilen; i++) {
    if (GET_OPCODE(piseq[i]) == OP_RETURN) {
      /* clear A argument (return self always) */
      piseq[i] &= ((1 << 23) - 1);
    }
  }
}

mrb_value
mrb_instance_new_fast(mrb_state *mrb, mrb_value cv)
{
  mrb_value obj, blk;
  mrb_value *argv;
  mrb_int argc;
  mrb_sym initsym = mrb_intern_lit(mrb, "initialize");
  struct RProc *m;
  struct RClass *c = mrb_class_ptr(cv);
  struct RObject *o;
  mrb_callinfo *initci;
  mrb_callinfo *cci;
  enum mrb_vtype ttype = MRB_INSTANCE_TT(c);

  mrb_get_args(mrb, "*&", &argv, &argc, &blk);

  if (c->tt == MRB_TT_SCLASS)
    mrb_raise(mrb, E_TYPE_ERROR, "can't create instance of singleton class");

  if (ttype == 0) ttype = MRB_TT_OBJECT;
  o = (struct RObject*)mrb_obj_alloc(mrb, ttype, c);
  obj = mrb_obj_value(o);

  m = mrb_method_search_vm(mrb, &c, initsym);
  if (m && !MRB_PROC_CFUNC_P(m)) {
    initci = mrb->c->ci;
    mrb->c->stack[0] = obj;

    patch_irep(mrb, m);

    cci = cipush(mrb);
    *cci = *initci;
    cci->stackent = mrb->c->stack;
    cci->target_class = NULL;	/* modify proc, irep, ... */
    
    cci->pc = m->body.irep->iseq;
    initci->proc = m;
    initci->mid = initsym;
    initci->target_class = c;
  }
  else {
    mrb_funcall_with_block(mrb, obj, initsym, argc, argv, blk);
  }

  return obj;
}

void
mrb_mruby_class_fast_gem_init(mrb_state* mrb) {
  struct RClass *cls;           /* Class */
  cls = mrb->class_class;

  mrb_define_method(mrb, cls, "new", mrb_instance_new_fast, MRB_ARGS_ANY());  /* 15.2.3.3.3 */
}

void
mrb_mruby_class_fast_gem_final(mrb_state* mrb) {
}
