/*****
 * name.cc
 * Andy Hammerlindl2002/07/14
 *
 * Qualified names (such as x, f, builtin.sin, a.b.c.d, etc.) can be used
 * either as varibles or a type names.  This class stores qualified
 * names used in nameExp and nameTy in the abstract syntax, and
 * implements the exp and type functions.
 *****/

#include "name.h"
#include "frame.h"
#include "record.h"
#include "coenv.h"
#include "inst.h"

namespace absyntax {
using namespace types;
using trans::access;
using trans::qualifiedAccess;
using trans::action;
using trans::READ;
using trans::WRITE;
using trans::CALL;
using vm::inst;


// Checks if a varEntry returned from coenv::lookupExactVar is ambiguous,
// an reports an error if it is.
static bool checkAmbiguity(position pos, symbol *s, varEntry *v)
{
  types::ty *t = v->getType();
  assert(t);

  if (t->kind == types::ty_overloaded) {
    em->error(pos);
    *em << "variable of name \'" << *s << "\' is ambiguous";
    return false;
  }
  else
    // All is well
    return true;
}

types::ty *signatureless(types::ty *t) {
  if (overloaded *o=dynamic_cast<overloaded *>(t))
    return o->signatureless();
  else
    return (t && !t->getSignature()) ? t : 0;
}


void name::forceEquivalency(action act, coenv &e,
                            types::ty *target, types::ty *source)
{
  if (act == READ)
    e.implicitCast(getPos(), target, source);
  else if (!equivalent(target, source)) {
    em->compiler(getPos());
    *em << "type mismatch in variable: "
      << *target
      << " vs " << *source;
  }
}

frame *name::frameTrans(coenv &e)
{
  types::ty *t=signatureless(varGetType(e));

  if (t && t->kind == types::ty_record) {
    varTrans(READ, e, t);
    return ((record *)t)->getLevel();
  }
  return 0;
}

types::ty *name::getType(coenv &e, bool tacit)
{
  types::ty *t=signatureless(varGetType(e));
  if (!tacit && t && t->kind == ty_error)
    // Report errors associated with regarding the name as a variable.
    varTrans(trans::READ, e, t);
  return t ? t : typeTrans(e, tacit);
}


varEntry *simpleName::getVarEntry(coenv &e)
{
  types::ty *t=signatureless(varGetType(e));
  return t ? e.e.lookupVarByType(id, t) : 0;
}
  
void simpleName::varTrans(action act, coenv &e, types::ty *target)
{
  //varEntry *v = e.e.lookupExactVar(id, target->getSignature());
  varEntry *v = e.e.lookupVarByType(id, target);
  
  if (v) {
    if (checkAmbiguity(getPos(), id, v)) {
      v->encode(act, getPos(), e.c);
      forceEquivalency(act, e, target, v->getType());
    }
  }
  else {
    em->error(getPos());
    *em << "no matching variable of name \'" << *id << "\'";
  }
}

types::ty *simpleName::varGetType(coenv &e)
{
  return e.e.varGetType(id);
}

types::ty *simpleName::typeTrans(coenv &e, bool tacit)
{
  types::ty *t = e.e.lookupType(id);
  if (t) {
    if (t->kind == types::ty_overloaded) {
      if (!tacit) {
	em->error(getPos());
	*em << "type of name \'" << *id << "\' is ambiguous";
      }
      return primError();
    }
    return t;
  }
  else {
    // NOTE: Could call getModule here.
    if (!tacit) {
      em->error(getPos());
      *em << "no type of name \'" << *id << "\'";
    }
    return primError();
  }
}

tyEntry *simpleName::tyEntryTrans(coenv &e)
{
  return new tyEntry(typeTrans(e),0);
}

void simpleName::prettyprint(ostream &out, int indent)
{
  prettyindent(out, indent);
  out << "simpleName '" << *id << "'\n";
}


record *qualifiedName::castToRecord(types::ty *t, bool tacit)
{
  switch (t->kind) {
    case ty_overloaded:
      if (!tacit) {
        em->compiler(qualifier->getPos());
        *em << "name::getType returned overloaded";
      }
      return 0;
    case ty_record:
      return (record *)t;
    case ty_error:
      return 0;
    default:
      if (!tacit) {
        em->error(qualifier->getPos());
        *em << "type \'" << *t << "\' is not a structure";
      }
      return 0;
  }
}

bool qualifiedName::varTransVirtual(action act, coenv &e,
                                    types::ty *target, types::ty *qt)
{
  varEntry *v = qt->virtualField(id, target->getSignature());
  if (v) {
    // Push qualifier onto stack.
    qualifier->varTrans(READ, e, qt);

    if (act == WRITE) {
      em->error(getPos());
      *em << "virtual field '" << *id << "' of '" << *qt
          << "' cannot be modified";
    }
    else {
      // Call instead of reading as it is a virtual field.
      v->encode(CALL, getPos(), e.c);
      e.implicitCast(getPos(), target, v->getType());

      if (act == CALL)
        // In this case, the virtual field will construct a vm::func object
        // and push it on the stack.
        // Then, pop and call the function.
        e.c.encode(inst::popcall);
    }

    // A virtual field was used.
    return true;
  }

  // No virtual field.
  return false;
}

void qualifiedName::varTransField(action act, coenv &e,
                                  types::ty *target, record *r)
{
  //v = r->lookupExactVar(id, target->getSignature());
  varEntry *v = r->lookupVarByType(id, target);

  if (v) {
    frame *f = qualifier->frameTrans(e);
    if (f)
      v->encode(act, getPos(), e.c, f);
    else
      v->encode(act, getPos(), e.c);

    forceEquivalency(act, e, target, v->getType());
  }
  else {
    em->error(getPos());
    *em << "no matching field of name \'" << *id << "\' in \'" << *r << "\'";
  }
}

void qualifiedName::varTrans(action act, coenv &e, types::ty *target)
{
  types::ty *qt = qualifier->getType(e);

  // Use virtual fields if applicable.
  if (varTransVirtual(act, e, target, qt))
    return;

  record *r = castToRecord(qt);
  if (r)
    varTransField(act, e, target, r);
}

types::ty *qualifiedName::varGetType(coenv &e)
{
  types::ty *qt = qualifier->getType(e, true);

  // Look for virtual fields.
  types::ty *t = qt->virtualFieldGetType(id);
  if (t)
    return t;

  record *r = castToRecord(qt, true);
  return r ? r->varGetType(id) : 0;
}

varEntry *mergeEntries(varEntry *qv, varEntry *v)
{
  if (qv) {
    if (v) {
      frame *f=dynamic_cast<record *>(qv->getType())->getLevel();
      return new varEntry(v->getType(),
                          new qualifiedAccess(qv->getLocation(),
                                              f,
                                              v->getLocation()));
    }
    else
      return qv;
  }
  else
    return v;
}

trans::varEntry *qualifiedName::getVarEntry(coenv &e)
{
  varEntry *qv = qualifier->getVarEntry(e);

  types::ty *qt = qualifier->getType(e, true);
  record *r = castToRecord(qt, true);
  if (r) {
    types::ty *t = signatureless(r->varGetType(id));
    varEntry *v = t ? r->lookupVarByType(id, t) : 0;
    return mergeEntries(qv,v);
  }
  else
    return qv;
}

types::ty *qualifiedName::typeTrans(coenv &e, bool tacit)
{
  types::ty *rt = qualifier->getType(e, tacit);

  record *r = castToRecord(rt, tacit);
  if (!r)
    return primError();

  types::ty *t = r->lookupType(id);
  if (t) {
    if (t->kind == types::ty_overloaded) {
      if (!tacit) {
	em->error(getPos());
	*em << "type of name \'" << *id << "\' is ambiguous";
      }
      return primError();
    }
    return t;
  }
  else {
    if (!tacit) {
      em->error(getPos());
      *em << "no matching field or type of name \'" << *id << "\' in \'"
          << *r << "\'";
    }
    return primError();
  }
}

tyEntry *qualifiedName::tyEntryTrans(coenv &e)
{
  return new tyEntry(typeTrans(e), qualifier->getVarEntry(e));
}
void qualifiedName::prettyprint(ostream &out, int indent)
{
  prettyindent(out, indent);
  out << "qualifiedName '" << *id << "'\n";

  qualifier->prettyprint(out, indent+1);
}

} // namespace absyntax
