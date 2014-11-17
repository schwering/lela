// vim:filetype=cpp:textwidth=80:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014 schwering@kbsg.rwth-aachen.de
//
// The ECLiPSe-CLP interface to ESBL provides the following external predicates:
//
// :- external(kcontext/2, p_kcontext).
// :- external(bcontext/3, p_bcontext).
//
// These two can be used to initialize a BAT. The first one refers to normal
// ESL BATs, whereas the second one refers to ESBL BATs which contain multiple
// setups. In both cases, the first argument is an atom by which afterwards the
// BAT can be accessed. The second argument is the name of the BAT, which must
// be hard-coded in Context::CreateInstance(). For belief BATs, the third
// argument specifies the k parameter.
//
// :- external(guarantee_consistency/2, p_guarantee_consistency).
// :- external(add_sensing_result/4, p_add_sensing_result).
//
// These two interface GuaranteeConsistency() and AddSensingResult() from the
// Setup and Setups classes. The first argument is the atom for which the BAT
// was initialized. The second parameter for guarantee_consistency/2 is the k
// parameter. The other parameters of add_sensing_result/4 are the list of
// actions representing the current situation, the name of the executed action,
// and finally the atom `true' or the atom `false' to indicate the binary
// sensing result.
//
// :- external(inconsistent/2, p_inconsistent).
// :- external(entails/3, p_entails).
//
// These two interface Inconsistent() and Entails() from the Setup and Setups
// classes. The first argument again is the atom for which the BAT was
// initialized. The second parameter is the k-parameter. The last parameter for
// entails/3 is the formula (see below).
//
// :- external(enable_regression/1, p_enable_regression).
// :- external(disable_regression/1, p_disable_regression).
// :- external(is_regression/1, p_is_regression).
//
// The first two switch enable or disable regression. If it is disabled, dynamic
// reasoning is done as in ESL using the successor state axioms and unit
// propagation. If regression is enabled, fluent, POSS and SF predicates are
// regressed to the initial situation. Hence, reasoning is only concerned with
// the initial situation (unless one of the predicates cannot be regressed).
// Finally is_regression/1 succeeds iff regression is enabled.
// By default, regression is disabled.
// The only argument should be the atom representing the BAT.
//
// :- external(register_pred/3, p_register_pred).
// :- external(register_name/4, p_register_name).
//
// Finally we have these two to register predicates or standard names which did
// not occur in the BAT. In both cases, the first argument is the atom
// representing the BAT. The second and third argument for register_pred/3 are a
// Prolog term that names the predicate and an integer that represents its ID.
// For register_name/4, we have an atom that names the term, and then the
// standard name's ID, and lastly its sort, which may be either given as integer
// or by providing another standard name, whose sort is then taken.
//
// The formulas are of the following form:
//   P(T1,...,Tk)           (predicate)
//   ~ Alpha                (negation)
//   (Alpha1 ^ Alpha2)      (conjunction)
//   (Alpha1 v Alpha2)      (disjunction)
//   (Alpha1 -> Alpha2)     (implication)
//   (Alpha1 <-> Alpha2)    (equivalence)
//   exists(X, Sort, Alpha) (existential)
//   forall(X, Sort, Alpha) (universal)
//   (A : Alpha)            (action)
// where P(T1,...,Tk) is a Prolog literal and P either string-matches a
// predicate symbol from the BAT or has been registered through register_pred/3
// beforehand; Alpha, Alpha1, Alpha2 are formulas; X are arbitrary Prolog terms
// that represent variables; A,T1,...,Tk are either Prolog terms that represent
// variables bound by exists(.,.,.) or forall(.,.,.) or atoms that represent
// standard names, which must either string-match the standard names from the
// BAt or have been registered through register_name/4 before; and Sort is
// either an integer representing the sort ID or an atom representing a
// standard name whose sort is then taken.

extern "C" {
#include <eclipse-clp/config.h>
#include <eclipse-clp/ec_public.h>
}
#include <eclipse-clp/eclipseclass.h>

#include <cassert>
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <./formula.h>
#include <./maybe.h>
#include <./bat.h>
#include <./ecai2014.h>
#include <./kr2014.h>
#include <./kitchen.h>

namespace esbl {

const std::string NEGATION = "~";
const std::string CONJUNCTION = "^";
const std::string DISJUNCTION = "v";
const std::string IMPLICATION = "->";
const std::string EQUIVALENCE = "<->";
const std::string EXISTS = "exists";
const std::string FORALL = "forall";
const std::string ACTION = ":";

struct EC_word_comparator {
  bool operator()(const EC_word& lhs, const EC_word& rhs) const {
    return compare(lhs, rhs) < 0;
  }
};

class PredBuilder {
 public:
  explicit PredBuilder(bats::Bat* bat) : bat_(bat) {}
  PredBuilder(const PredBuilder&) = delete;
  PredBuilder& operator=(const PredBuilder&) = delete;

  Maybe<Atom::PredId> Get(EC_word w) {
    const auto it = preds_.find(w);
    if (it != preds_.end()) {
      return Just(it->second);
    }
    EC_atom a;
    if (w.is_atom(&a) == EC_succeed) {
      const std::string s = a.name();
      const auto p = bat_->StringToPred(s);
      if (p) {
        return p;
      }
    }
    EC_functor f;
    if (w.functor(&f) == EC_succeed) {
      const std::string s = f.name();
      const auto p = bat_->StringToPred(s);
      if (p) {
        return p;
      }
    }
    return Nothing;
  }

  bool Register(EC_word w, Atom::PredId p) {
    if (preds_.find(w) != preds_.end()) {
      return false;
    }
    preds_[w] = p;
    return true;
  }

 private:
  bats::Bat* bat_;
  std::map<EC_word, Atom::PredId, EC_word_comparator> preds_;
};

class TermBuilder {
 public:
  explicit TermBuilder(bats::Bat* bat) : bat_(bat) {}
  TermBuilder(const TermBuilder&) = delete;
  TermBuilder& operator=(const TermBuilder&) = delete;

  Maybe<StdName> GetName(EC_atom a) {
    const auto it = names_.find(a);
    if (it != names_.end()) {
      return Just(it->second);
    }
    const std::string s = a.name();
    const auto p = bat_->StringToName(s);
    if (p) {
      return p;
    }
    return Nothing;
  }

  Maybe<Variable> GetVar(EC_word w) {
    const auto it = vars_.find(w);
    if (it != vars_.end() && !it->second.empty()) {
      return Just(it->second.front());
    }
    return Nothing;
  }

  Variable PushVar(EC_word w, Term::Sort sort) {
    const Variable x = bat_->tf().CreateVariable(sort);
    vars_[w].push_front(x);
    return x;
  }

  Maybe<Variable> PopVar(EC_word w) {
    const auto it = vars_.find(w);
    if (it != vars_.end() && !it->second.empty()) {
      const Variable x = it->second.front();
      it->second.pop_front();
      return Just(x);
    }
    return Nothing;
  }

  Maybe<Term> Get(EC_word t) {
    EC_atom a;
    if (t.is_atom(&a) == EC_succeed) {
      const auto p = GetName(a);
      if (p) {
        return p;
      }
    }
    const auto p = GetVar(t);
    if (p) {
      return p;
    }
    return Nothing;
  }

  bool Register(EC_word w, const StdName& n) {
    EC_atom a;
    if (w.is_atom(&a) != EC_succeed) {
      return false;
    }
    if (names_.find(a) != names_.end()) {
      return false;
    }
    names_[a] = n;
    return true;
  }

 private:
  bats::Bat* bat_;
  std::map<EC_atom, StdName, EC_word_comparator> names_;
  std::map<EC_word, std::deque<Variable>, EC_word_comparator> vars_;
};

class SortBuilder {
 public:
  SortBuilder(bats::Bat* bat, TermBuilder* tb) : bat_(bat), tb_(tb) {}
  SortBuilder(const SortBuilder&) = delete;
  SortBuilder& operator=(const SortBuilder&) = delete;

  Maybe<Term::Sort> Get(EC_word w) {
    EC_atom a;
    if (w.is_atom(&a) == EC_succeed) {
      const std::string s = a.name();
      const auto p = bat_->StringToSort(s);
      if (p) {
        return p;
      }
    }
    long l;
    if (w.is_long(&l) == EC_succeed) {
      return Just(static_cast<Term::Id>(l));
    }
    if (w.is_atom(&a) == EC_succeed) {
      const auto p = tb_->GetName(a);
      if (p) {
        return Just(p.val.sort());
      }
    }
    return Nothing;
  }

 private:
  bats::Bat* bat_;
  TermBuilder* tb_;
};

class FormulaBuilder {
 public:
  explicit FormulaBuilder(bats::Bat* bat)
    : pred_builder_(bat),
      term_builder_(bat),
      sort_builder_(bat, &term_builder_) {}

  Maybe<Formula::Ptr> Build(EC_word ec_alpha);

  PredBuilder& pred_builder() { return pred_builder_; }
  TermBuilder& term_builder() { return term_builder_; }
  SortBuilder& sort_builder() { return sort_builder_; }

 private:
  PredBuilder pred_builder_;
  TermBuilder term_builder_;
  SortBuilder sort_builder_;
};

#define ARG_FORMULA(ec_alpha, beta, i) \
  EC_word ec_##beta;\
  if (ec_alpha.arg(i, ec_##beta) != EC_succeed) {\
    return Nothing;\
  }\
  auto p_##beta = Build(ec_##beta);\
  if (!p_##beta) {\
    return Nothing;\
  }\
  Formula::Ptr beta(std::move(p_##beta.val))

#define ARG_QVAR(ec_alpha, var, sort, i) \
  EC_word ec_##var;\
  if (ec_alpha.arg(i, ec_##var) != EC_succeed) {\
    return Nothing;\
  }\
  const auto var = term_builder_.PushVar(ec_##var, sort);

#define ARG_TERM(ec_alpha, term, i) \
  EC_word ec_##term;\
  if (ec_alpha.arg(i, ec_##term) != EC_succeed) {\
    return Nothing;\
  }\
  const auto p_##term = term_builder_.Get(ec_##term);\
  if (!p_##term) {\
    return Nothing;\
  }\
  const Term term = p_##term.val;

#define ARG_SORT(ec_alpha, sort, i) \
  EC_word ec_##sort;\
  if (ec_alpha.arg(i, ec_##sort) != EC_succeed) {\
    return Nothing;\
  }\
  const auto p_##sort = sort_builder_.Get(ec_##sort);\
  if (!p_##sort) {\
    return Nothing;\
  }\
  const Term::Sort sort = p_##sort.val;

Maybe<Formula::Ptr> FormulaBuilder::Build(EC_word ec_alpha)
{
  EC_functor f;
  EC_atom a;
  const bool is_functor = ec_alpha.functor(&f) == EC_succeed;
  const bool is_atom = ec_alpha.is_atom(&a) == EC_succeed;
  if (is_functor && f.name() == NEGATION && f.arity() == 1) {
    ARG_FORMULA(ec_alpha, beta, 1);
    beta = Formula::Neg(std::move(beta));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == DISJUNCTION && f.arity() == 2) {
    ARG_FORMULA(ec_alpha, beta1, 1);
    ARG_FORMULA(ec_alpha, beta2, 2);
    Formula::Ptr beta = Formula::Or(std::move(beta1), std::move(beta2));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == CONJUNCTION && f.arity() == 2) {
    ARG_FORMULA(ec_alpha, beta1, 1);
    ARG_FORMULA(ec_alpha, beta2, 2);
    Formula::Ptr beta = Formula::And(std::move(beta1), std::move(beta2));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == IMPLICATION && f.arity() == 2) {
    ARG_FORMULA(ec_alpha, beta1, 1);
    ARG_FORMULA(ec_alpha, beta2, 2);
    Formula::Ptr beta = Formula::Or(Formula::Neg(std::move(beta1)),
                                    std::move(beta2));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == EQUIVALENCE && f.arity() == 2) {
    ARG_FORMULA(ec_alpha, beta1a, 1);
    ARG_FORMULA(ec_alpha, beta2a, 2);
    Formula::Ptr beta1b = beta1a->Copy();
    Formula::Ptr beta2b = beta1a->Copy();
    Formula::Ptr beta =
        Formula::And(Formula::Or(Formula::Neg(std::move(beta1a)),
                                 std::move(beta2a)),
                     Formula::Or(std::move(beta1b),
                                 Formula::Neg(std::move(beta2b))));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == EXISTS && f.arity() == 3) {
    ARG_SORT(ec_alpha, sort, 2);
    ARG_QVAR(ec_alpha, var, sort, 1);
    ARG_FORMULA(ec_alpha, beta, 3);
    term_builder_.PopVar(ec_var);
    beta = Formula::Exists(var, std::move(beta));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == FORALL && f.arity() == 3) {
    ARG_SORT(ec_alpha, sort, 2);
    ARG_QVAR(ec_alpha, var, sort, 1);
    ARG_FORMULA(ec_alpha, beta, 3);
    term_builder_.PopVar(ec_var);
    beta = Formula::Forall(var, std::move(beta));
    return Just(std::move(beta));
  } else if (is_functor && f.name() == ACTION && f.arity() == 2) {
    ARG_TERM(ec_alpha, term, 1);
    ARG_FORMULA(ec_alpha, beta, 2);
    beta = Formula::Act(term, std::move(beta));
    return Just(std::move(beta));
  } else if (is_functor) {
    const auto p = pred_builder_.Get(ec_alpha);
    if (!p) {
      return Nothing;
    }
    const bool sign = true;
    TermSeq args;
    for (int i = 1; i <= f.arity(); ++i) {
        ARG_TERM(ec_alpha, term, i);
        args.push_back(term);
    }
    const Literal l({}, sign, p.val, args);
    return Just(Formula::Lit(l));
  } else if (is_atom) {
    const auto p = pred_builder_.Get(ec_alpha);
    if (!p) {
      return Nothing;
    }
    const bool sign = true;
    const Literal l({}, sign, p.val, {});
    return Just(Formula::Lit(l));
  } else {
    return Nothing;
  }
}

class Context {
 public:
  static const t_ext_type MethodTable;

  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  static Context* CreateInstance(EC_word ec_key, EC_word ec_bat, EC_word ec_k) {
    const auto it = instances_.find(ec_key);
    if (it != instances_.end()) {
      DeleteInstance(it->second);
    }

    long k;
    ec_k.is_long(&k);

    EC_atom a;
    if (ec_bat.is_atom(&a) != EC_succeed) {
      return nullptr;
    }
    std::string s = a.name();
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    std::unique_ptr<bats::Bat> bat;
    if (s == "KR2014") {
      bat = std::unique_ptr<bats::Bat>(new bats::Kr2014());
    } else if (s == "ECAI2014") {
      bat = std::unique_ptr<bats::Bat>(new bats::Ecai2014(k));
    } else if (s == "KITCHEN") {
      bat = std::unique_ptr<bats::Bat>(new bats::Kitchen());
    }
    if (!bat) {
      return nullptr;
    }
    Context* ctx = new Context(std::move(bat));
    instances_[ec_key] = ctx;
    return ctx;
  }

  static Context* GetInstance(EC_word ec_key) {
    const auto it = instances_.find(ec_key);
    if (it != instances_.end()) {
      return it->second;
    }
    return nullptr;
  }

  static void DeleteInstance(void* ptr) {
    DeleteInstance(static_cast<Context*>(ptr));
  }

  static void DeleteInstance(Context* self) {
    for (auto it = instances_.begin(); it != instances_.end(); ++it) {
      if (it->second == self) {
        instances_.erase(it);
        break;
      }
    }
    delete self;
  }

  void UseRegression(bool enable) { regression_enabled_ = enable; }
  bool UseRegression() const { return regression_enabled_; }

  Term::Factory* tf() { return &bat_->tf(); }

  bats::Bat& bat() { return *bat_; };
  FormulaBuilder& formula_builder() { return formula_builder_; }
  PredBuilder& pred_builder() { return formula_builder_.pred_builder(); }
  TermBuilder& term_builder() { return formula_builder_.term_builder(); }
  SortBuilder& sort_builder() { return formula_builder_.sort_builder(); }

 private:
  explicit Context(std::unique_ptr<bats::Bat> bat)
      : bat_(std::move(bat)), formula_builder_(bat_.get()) {}

  static std::map<EC_word, Context*, EC_word_comparator> instances_;

  std::unique_ptr<bats::Bat> bat_;
  FormulaBuilder formula_builder_;
  bool regression_enabled_;
};

const t_ext_type Context::MethodTable = {
    Context::DeleteInstance,  // free
    NULL,  // copy
    NULL,  // mark_dids
    NULL,  // string_size
    NULL,  // to_string
    NULL,  // equal
    NULL,  // remote_copy
    NULL,  // get
    NULL   // set
};

std::map<EC_word, Context*, EC_word_comparator> Context::instances_;

}

extern "C"
int p_kcontext() {
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_bat = EC_arg(2);

  Context* ctx = Context::CreateInstance(ec_key, ec_bat, EC_word());
  return ctx != nullptr ? PSUCCEED : PFAIL;
}

extern "C"
int p_bcontext() {
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_bat = EC_arg(2);
  EC_word ec_k = EC_arg(3);

  long l;
  if (ec_k.is_long(&l) != EC_succeed) {
    return TYPE_ERROR;
  }

  Context* ctx = Context::CreateInstance(ec_key, ec_bat, ec_k);
  return ctx != nullptr ? PSUCCEED : PFAIL;
}

extern "C"
int p_register_pred()
{
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_w = EC_arg(2);
  EC_word ec_p = EC_arg(3);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  long p;
  if (ec_p.is_long(&p) != EC_succeed) {
    return TYPE_ERROR;
  }

  return ctx->pred_builder().Register(ec_w, p) ? PSUCCEED : PFAIL;
}

extern "C"
int p_register_name()
{
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_w = EC_arg(2);
  EC_word ec_name = EC_arg(3);
  EC_word ec_sort = EC_arg(4);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  EC_atom a;
  if (ec_w.is_atom(&a) != EC_succeed) {
    return TYPE_ERROR;
  }

  const auto p = ctx->sort_builder().Get(ec_sort);
  if (!p) {
    return TYPE_ERROR;
  }
  const Term::Sort sort = p.val;

  long name_id;
  if (ec_name.is_long(&name_id) != EC_succeed) {
    return TYPE_ERROR;
  }
  const StdName name = ctx->tf()->CreateStdName(name_id, sort);

  return ctx->term_builder().Register(a, name) ? PSUCCEED : PFAIL;
}

extern "C"
int p_enable_regression()
{
  using namespace esbl;

  EC_word ec_key = EC_arg(1);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  ctx->UseRegression(true);

  return PSUCCEED;
}

extern "C"
int p_disable_regression()
{
  using namespace esbl;

  EC_word ec_key = EC_arg(1);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  ctx->UseRegression(false);

  return PSUCCEED;
}

extern "C"
int p_is_regression()
{
  using namespace esbl;

  EC_word ec_key = EC_arg(1);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  return ctx->UseRegression() ?  PSUCCEED : PFAIL;
}

extern "C"
int p_guarantee_consistency()
{
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_k = EC_arg(2);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  long k;
  if (ec_k.is_long(&k) != EC_succeed) {
    return TYPE_ERROR;
  }

  KBat* kbat = dynamic_cast<KBat*>(&ctx->bat());
  BBat* bbat = dynamic_cast<BBat*>(&ctx->bat());
  assert(static_cast<bool>(kbat) != static_cast<bool>(bbat));
  if (kbat) {
    kbat->setup().GuaranteeConsistency(k);
    return PSUCCEED;
  }
  if (bbat) {
    bbat->setups().GuaranteeConsistency(k);
    return PSUCCEED;
  }
  return PFAIL;
}

extern "C"
int p_add_sensing_result()
{
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_z = EC_arg(2);
  EC_word ec_t = EC_arg(3);
  EC_word ec_r = EC_arg(4);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  TermSeq z;
  for (EC_word hd, tl = ec_z; tl.is_list(hd, tl) == EC_succeed; ) {
    const auto p = ctx->term_builder().Get(hd);
    if (!p) {
      return TYPE_ERROR;
    }
    z.push_back(p.val);
  }

  EC_atom a;
  if (ec_t.is_atom(&a) != EC_succeed) {
    return TYPE_ERROR;
  }
  const auto p = ctx->term_builder().GetName(a);
  if (!p) {
    return TYPE_ERROR;
  }
  const StdName t = p.val;

  if (ec_r.is_atom(&a) != EC_succeed) {
    return TYPE_ERROR;
  }
  std::string s = a.name();
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  bool r;
  if (s == "TRUE") {
    r = true;
  } else if (s == "FALSE") {
    r = false;
  } else {
    return TYPE_ERROR;
  }

  KBat* kbat = dynamic_cast<KBat*>(&ctx->bat());
  BBat* bbat = dynamic_cast<BBat*>(&ctx->bat());
  assert(static_cast<bool>(kbat) != static_cast<bool>(bbat));
  if (kbat) {
    if (!ctx->UseRegression()) {
      kbat->setup().AddClause(Clause(Ewff::TRUE, {SfLiteral(z, t, r)}));
    } else {
      Formula::Lit(SfLiteral(z, t, r))->Regress(ctx->tf(), ctx->bat())->AddToSetup(ctx->tf(), &kbat->setup());
    }
    return PSUCCEED;
  }
  if (bbat) {
    if (!ctx->UseRegression()) {
      bbat->setups().AddClause(Clause(Ewff::TRUE, {SfLiteral(z, t, r)}));
    } else {
      Formula::Lit(SfLiteral(z, t, r))->Regress(ctx->tf(), ctx->bat())->AddToSetups(ctx->tf(), &bbat->setups());
    }
    return PSUCCEED;
  }
  return PFAIL;
}

extern "C"
int p_inconsistent()
{
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_k = EC_arg(2);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  long k;
  if (ec_k.is_long(&k) != EC_succeed) {
    return TYPE_ERROR;
  }

  KBat* kbat = dynamic_cast<KBat*>(&ctx->bat());
  BBat* bbat = dynamic_cast<BBat*>(&ctx->bat());
  assert(static_cast<bool>(kbat) != static_cast<bool>(bbat));
  if (kbat) {
    return kbat->setup().Inconsistent(k) ? PSUCCEED : PFAIL;
  }
  if (bbat) {
    return bbat->setups().Inconsistent(k) ? PSUCCEED : PFAIL;
  }
  return PFAIL;
}

extern "C"
int p_entails()
{
  using namespace esbl;
  using namespace esbl::bats;

  EC_word ec_key = EC_arg(1);
  EC_word ec_alpha = EC_arg(2);
  EC_word ec_k = EC_arg(3);

  Context* ctx = Context::GetInstance(ec_key);
  if (!ctx) {
    return RANGE_ERROR;
  }

  long k;
  if (ec_k.is_long(&k) != EC_succeed) {
    return TYPE_ERROR;
  }

  Maybe<Formula::Ptr> maybe_alpha = ctx->formula_builder().Build(ec_alpha);
  if (!maybe_alpha) {
    return TYPE_ERROR;
  }
  Formula::Ptr alpha = !ctx->UseRegression()
      ? std::move(maybe_alpha.val)
      : maybe_alpha.val->Regress(ctx->tf(), ctx->bat());

  KBat* kbat = dynamic_cast<KBat*>(&ctx->bat());
  BBat* bbat = dynamic_cast<BBat*>(&ctx->bat());
  assert(static_cast<bool>(kbat) != static_cast<bool>(bbat));
  if (kbat) {
    return alpha->EntailedBy(&kbat->tf(), &kbat->setup(), k) ? PSUCCEED : PFAIL;
  }
  if (bbat) {
    return alpha->EntailedBy(&bbat->tf(), &bbat->setups(), k) ? PSUCCEED : PFAIL;
  }
  return PFAIL;
}
