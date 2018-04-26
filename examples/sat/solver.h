// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014-2018 Christoph Schwering
// Licensed under the MIT license. See LICENSE file in the project root.

#ifndef LIMBO_SOLVER_H_
#define LIMBO_SOLVER_H_

#include <cstdlib>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <limbo/literal.h>
#include <limbo/term.h>

#include <limbo/format/output.h>

#include "clause.h"

namespace limbo {

template<typename T, typename Index>
struct IndexOf { Index operator()(const T t) const { return t.index(); } };

template<typename T, typename Index>
struct FromIndex { T operator()(const Index i) const { return T(i); } };

template<typename T>
struct MakeNull { T operator()() const { return T(); } };

template<typename Key, typename Val, typename Index = int,
         typename IndexOf = IndexOf<Key, Index>,
         typename MakeNull = MakeNull<Val>>
class DenseMap {
 public:
  using Vec = std::vector<Val>;
  using value_type = typename Vec::value_type;
  using reference = typename Vec::reference;
  using const_reference = typename Vec::const_reference;
  using iterator = typename Vec::iterator;
  using const_iterator = typename Vec::const_iterator;

  DenseMap() = default;

  DenseMap(const DenseMap&) = default;
  DenseMap& operator=(const DenseMap& c) = default;
  DenseMap(DenseMap&&) = default;
  DenseMap& operator=(DenseMap&& c) = default;

  void Capacitate(Key k) { Capacitate(index_of_(k)); }
  void Capacitate(Index i) {
    if (i >= vec_.size()) {
      vec_.resize(i + 1, make_null_());
    }
  }
  void Clear() { vec_.clear(); }

  Index upper_bound() const { return vec_.size(); }

  reference operator[](Index i) { assert(i < vec_.size()); return vec_[i]; }
  const_reference operator[](Index i) const { assert(i < vec_.size()); return vec_[i]; }

  reference operator[](Key key) { return operator[](index_of_(key)); }
  const_reference operator[](Key key) const { return operator[](index_of_(key)); }

  iterator begin() { return vec_.begin(); }
  iterator end()   { return vec_.end(); }

  const_iterator begin() const { return vec_.begin(); }
  const_iterator end()   const { return vec_.end(); }

 private:
  IndexOf index_of_;
  MakeNull make_null_;
  Vec vec_;
};

template<typename T, typename Index = int,
         typename IndexOf = IndexOf<T, Index>,
         typename MakeNull = MakeNull<T>>
class DenseSet {
 public:
  using Map = DenseMap<T, T, Index, IndexOf, MakeNull>;
  using value_type = typename Map::value_type;
  using reference = typename Map::reference;
  using const_reference = typename Map::const_reference;
  using iterator = typename Map::iterator;
  using const_iterator = typename Map::const_iterator;

  DenseSet() = default;

  DenseSet(const DenseSet&) = default;
  DenseSet& operator=(const DenseSet& c) = default;
  DenseSet(DenseSet&&) = default;
  DenseSet& operator=(DenseSet&& c) = default;

  void Capacitate(Index i) { map_.Capacitate(i); }
  void Capacitate(T x) { map_.Capacitate(x); }
  void Clear() { map_.Clear(); }

  Index upper_bound() const { return map_.upper_bound(); }

  bool Contains(T x) const { return x != make_null_() && map_[x] == x; }

  void Insert(T x) { assert(x != make_null_()); map_[x] = x; }
  void Remove(T x) { assert(x != make_null_()); map_[x] = make_null_(); }

  reference operator[](Index i) { return map_[i]; }
  const_reference operator[](Index i) const { return map_[i]; }

  iterator begin() { return map_.begin(); }
  iterator end()   { return map_.end(); }

  const_iterator begin() const { return map_.begin(); }
  const_iterator end()   const { return map_.end(); }

 private:
  MakeNull make_null_;
  Map map_;
};


template<typename T, typename Less, typename Index = int,
         typename IndexOf = IndexOf<T, Index>,
         typename MakeNull = MakeNull<T>>
class Heap {
 public:
  explicit Heap(Less less = Less()) : less_(less) { heap_.push_back(make_null_()); }

  Heap(const Heap&) = delete;
  Heap& operator=(const Heap& c) = delete;
  Heap(Heap&&) = default;
  Heap& operator=(Heap&& c) = default;

  void Capacitate(T x) { index_.Capacitate(x); }
  void Capacitate(int i) { index_.Capacitate(i); }
  void Clear() { heap_.clear(); index_.Clear(); heap_.push_back(make_null_()); }

  int size()  const { return heap_.size() - 1; }
  bool empty() const { return heap_.size() == 1; }

  bool Contains(T x) const { return index_[x] != 0; }

  T Top() const { return size() >= 1 ? heap_[1] : make_null_(); }

  void Increase(T x) {
    assert(Contains(x));
    SiftUp(index_[x]);
  }

  void Insert(T x) {
    assert(!Contains(x));
    int i = heap_.size();
    heap_.push_back(x);
    index_[x] = i;
    SiftUp(i);
  }

  typename std::vector<T>::const_iterator begin() const { return heap_.begin(); }
  typename std::vector<T>::const_iterator end()   const { return heap_.end(); }

  void Remove(T x) {
    assert(Contains(x));
    const int i = index_[x];
    heap_[i] = heap_.back();
    index_[heap_[i]] = i;
    heap_.pop_back();
    index_[x] = 0;
    if (heap_.size() > i) {
      SiftDown(i);
    }
    assert(!Contains(x));
  }

  void Heapify() {
    for (int i = 1; i < heap_.size(); ++i) {
      SiftDown(i);
    }
    for (int i = heap_.size() - 1; i > 0; --i) {
      SiftUp(i);
    }
  }

 private:
  static int left(int i)   { return 2 * i; }
  static int right(int i)  { return 2 * i + 1; }
  static int parent(int i) { return i / 2; }

  void SiftUp(int i) {
    assert(i > 0 && i < heap_.size());
    T x = heap_[i];
    int p;
    while ((p = parent(i)) != 0 && less_(x, heap_[p])) {
      heap_[i] = heap_[p];
      index_[heap_[i]] = i;
      i = p;
    }
    heap_[i] = x;
    index_[x] = i;
    assert(std::all_of(heap_.begin() + 1, heap_.end(), [this](T x) { return heap_[index_[x]] == x; }));
  }

  void SiftDown(int i) {
    assert(i > 0 && i < heap_.size());
    T x = heap_[i];
    while (left(i) < heap_.size()) {
      const int min_child = right(i) < heap_.size() && less_(heap_[right(i)], heap_[left(i)]) ? right(i) : left(i);
      if (!less_(heap_[min_child], x)) {
        break;
      }
      heap_[i] = heap_[min_child];
      index_[heap_[i]] = i;
      i = min_child;
    }
    heap_[i] = x;
    index_[x] = i;
    assert(std::all_of(heap_.begin() + 1, heap_.end(), [this](T x) { return heap_[index_[x]] == x; }));
  }

  Less less_;
  MakeNull make_null_;
  std::vector<T> heap_;
  DenseMap<T, int, Index, IndexOf> index_;
};

class Solver {
 public:
  using uref_t = int;
  using cref_t = Clause::Factory::cref_t;
  using level_t = int;

  Solver() = default;

  template<typename ExtraNameFactory>
  void AddLiteral(Literal a, ExtraNameFactory extra_name = ExtraNameFactory()) {
    if (a.valid()) {
      return;
    }
    if (a.unsatisfiable()) {
      empty_clause_ = true;
      return;
    }
    assert(a.primitive());
    trail_.push_back(a);
    Register(a.lhs().sort(), a.lhs(), a.rhs(), extra_name(a.lhs().sort()));
  }

  template<typename ExtraNameFactory>
  void AddClause(const std::vector<Literal>& as, ExtraNameFactory extra_name = ExtraNameFactory()) {
    if (as.size() == 0) {
      empty_clause_ = true;
    } else if (as.size() == 1) {
      AddLiteral(as[0], extra_name);
    } else {
      const cref_t cr = clause_factory_.New(as);
      Clause& c = clause_factory_[cr];
      if (c.valid()) {
        clause_factory_.Delete(cr, as.size());
        return;
      }
      if (c.unsatisfiable()) {
        empty_clause_ = true;
        clause_factory_.Delete(cr, as.size());
        return;
      }
      assert(!c.valid());
      assert(c.primitive());
      assert(c.size() >= 1);
      if (c.size() == 1) {
        trail_.push_back(c[0]);
        clause_factory_.Delete(cr, as.size());
      } else {
        clauses_.push_back(cr);
        for (const Literal a : c) {
          Register(a.lhs().sort(), a.lhs(), a.rhs(), extra_name(a.lhs().sort()));
        }
      }
    }
  }

  void Init() {
    std::vector<Literal> lits = std::move(trail_);
    trail_.reserve(lits.size());
    for (const Literal a : lits) {
      if (falsifies(a)) {
        empty_clause_ = true;
        return;
      }
      Enqueue(a, kNullRef);
    }
    int n_clauses = clauses_.size();
    for (int i = 1; i < n_clauses; ++i) {
      const cref_t cr = clauses_[i];
      Clause& c = clause_factory_[cr];
      c.RemoveIf([this](Literal a) { return falsifies(a); });
      assert(!c.valid());
      if (c.unsatisfiable()) {
        empty_clause_ = true;
        clause_factory_.Delete(cr, c.size());
        return;
      } else if (satisfies(c)) {
        clause_factory_.Delete(cr, c.size());
        std::swap(clauses_[i--], clauses_[--n_clauses]);
      } else if (c.size() == 1) {
        Enqueue(c[0], kNullRef);
        clause_factory_.Delete(cr, c.size());
        std::swap(clauses_[i--], clauses_[--n_clauses]);
      } else {
        UpdateWatchers(cr, c);
      }
    }
    clauses_.resize(n_clauses);
    Propagate();
    int n_units = trail_.size();
    for (int i = 0; i < n_units; ++i) {
      const Literal a = trail_[i];
      if (!a.pos() && !model_[a.lhs()].null()) {
        std::swap(trail_[i--], trail_[--n_units]);
      }
      const cref_t cr = reason_of(a);
      if (cr != kNullRef && cr != kDomainRef) {
        const Clause& c = clause(cr);
        RemoveWatchers(cr, c);
        clause_factory_.Delete(cr, c.size());
        std::swap(clauses_[cr], clauses_[--n_clauses]);
      }
    }
    trail_.resize(n_units);
    clauses_.resize(n_clauses);
  }

  const Clause&                                 clause(cref_t cr) const { return clause_factory_[cr]; }
  const DenseSet<Term>&                         funcs() const { return funcs_; }
  const DenseMap<Symbol::Sort, DenseSet<Term>>& names() const { return names_; }
  const DenseSet<Term>&                         names(Term f) const { return names_[f.sort()]; }
  const DenseMap<Term, Term>&                   model() const { return model_; }

  template<typename ConflictPredicate, typename DecisionPredicate>
  int Solve(ConflictPredicate conflict_predicate = ConflictPredicate(),
            DecisionPredicate decision_predicate = DecisionPredicate()) {
    if (empty_clause_) {
      return -1;
    }
    std::vector<Literal> learnt;
    bool go = true;
    while (go) {
      const cref_t conflict = Propagate();
      if (conflict != kNullRef) {
        if (current_level() == kRootLevel) {
          return -1;
        }
        level_t btlevel;
        Analyze(conflict, &learnt, &btlevel);
        go &= conflict_predicate(current_level(), conflict, learnt, btlevel);
        Backtrack(btlevel);
        if (learnt.size() == 1) {
          const Literal a = learnt[0];
          assert(!falsifies(a));
          Enqueue(a, kNullRef);
        } else {
          const cref_t cr = clause_factory_.New<Clause::kGuaranteeNormalized>(learnt);
          const Clause& c = clause(cr);
          assert(c.size() >= 2);
          assert(!satisfies(c));
          assert(!falsifies(c[0]));
          assert(std::all_of(c.begin() + 1, c.end(), [this](Literal a) { return falsifies(a); }));
          clauses_.push_back(cr);
          UpdateWatchers(cr, c);
          Enqueue(c[0], cr);
        }
        learnt.clear();
      } else {
        const Term f = order_.Top();
        if (f.null()) {
          return 1;
        }
        const Term n = CandidateName(f);
        if (n.null()) {
          return -1;
        }
        NewLevel();
        const Literal a = Literal::Eq(f, n);
        Enqueue(a, kNullRef);
        go &= decision_predicate(current_level(), a);
      }
    }
    Backtrack(kRootLevel);
    return 0;
  }

 private:
  struct Data {  // meta data for a pair (f,n)
    Data() = default;

    void Update(bool neq, level_t l, cref_t r) {
      model_neq = neq;
      level = l;
      reason = r;
    }

    void Reset() {
      model_neq = false;
      level = 0;
      reason = kNullRef;
    }

    unsigned seen_subsumed :  1;  // auxiliary flag to keep track of seen trail literals
    unsigned wanted        :  1;  // auxiliary flag to keep track of seen trail literals
    unsigned occurs        :  1;  // true iff f occurs with n in added clauses or literals
    unsigned model_neq     :  1;  // true iff f != n was set or derived
    unsigned level         : 28;  // level at which f = n or f != n was set or derived
    cref_t reason;                // clause which derived f = n or f != n
  };

  static_assert(sizeof(Data) == 4 + sizeof(cref_t), "Data should be 4 + 4 bytes");

  struct ActivityCompare {
    explicit ActivityCompare(const DenseMap<Term, double>* a) : activity_(a) {}
    bool operator()(Term t1, Term t2) const { return (*activity_)[t1] > (*activity_)[t2]; }
   private:
    const DenseMap<Term, double>* activity_;
  };

  static constexpr cref_t kNullRef = 0;
  static constexpr cref_t kDomainRef = -1;
  static constexpr level_t kRootLevel = 1;

  void Register(const Symbol::Sort s, const Term f, const Term n, const Term extra_n) {
    CapacitateMaps(s, f, n, extra_n);
    if (!funcs_.Contains(f)) {
      funcs_.Insert(f);
      order_.Insert(f);
      names_[s].Insert(extra_n);
      domain_size_[f] += 1 - data_[f][extra_n].occurs;
      data_[f][extra_n].occurs = true;
    }
    domain_size_[f] += 1 - data_[f][n].occurs;
    data_[f][n].occurs = true;
    names_[s].Insert(n);
  }

  void UpdateWatchers(const cref_t cr, const Clause& c) {
    assert(&clause(cr) == &c);
    assert(!c.unsatisfiable());
    assert(!c.valid());
    assert(c.size() >= 2);
    assert(!falsifies(c[0]) || std::all_of(c.begin()+2, c.end(), [this](Literal a) { return falsifies(a); }));
    assert(!falsifies(c[1]) || std::all_of(c.begin()+2, c.end(), [this](Literal a) { return falsifies(a); }));
    const Term f0 = c[0].lhs();
    const Term f1 = c[1].lhs();
    watchers_[f0].push_back(cr);
    if (f0 != f1) {
      watchers_[f1].push_back(cr);
    }
  }

  void RemoveWatchers(const cref_t cr, const Clause& c) {
    assert(c.size() >= 2);
    for (int i = 0; i < 2; ++i) {
      const Term f = c[i].lhs();
      std::vector<cref_t>& ws = watchers_[f];
      ws.erase(std::find(ws.begin(), ws.end(), cr));
    }
  }

  cref_t Propagate() {
    cref_t conflict = kNullRef;
    while (trail_head_ < trail_.size() && conflict == kNullRef) {
      Literal a = trail_[trail_head_++];
      conflict = Propagate(a);
    }
    assert(conflict != kNullRef || std::all_of(clauses_.begin()+1, clauses_.end(), [this](cref_t cr) { const Clause& c = clause(cr); return satisfies(c) || !falsifies(c[0]) || std::all_of(c.begin()+2, c.end(), [this](Literal a) { return falsifies(a); }); }));
    assert(conflict != kNullRef || std::all_of(clauses_.begin()+1, clauses_.end(), [this](cref_t cr) { const Clause& c = clause(cr); return satisfies(c) || !falsifies(c[1]) || std::all_of(c.begin()+2, c.end(), [this](Literal a) { return falsifies(a); }); }));
    return conflict;
  }

  cref_t Propagate(const Literal a) {
    assert(a.primitive());
    cref_t conflict = kNullRef;
    const Term f = a.lhs();
    std::vector<cref_t>& ws = watchers_[f];
    auto cr_ptr1 = ws.begin();
    auto cr_ptr2 = cr_ptr1;
    while (cr_ptr1 != ws.end()) {
      const cref_t cr = *cr_ptr1;
      Clause& c = clause_factory_[cr];
      const Term f0 = c[0].lhs();
      const Term f1 = c[1].lhs();

      // remove outdated watcher, skip
      if (f0 != f && f1 != f) {
        ++cr_ptr1;
        continue;
      }
      assert(c[0].lhs() == f || c[1].lhs() == f);

      // c[w >> 1] is falsified (c[1] if it is, else c[0])
      // c[1 - (w >> 1)] is the other literal
      char w = (static_cast<char>(falsifies(c[1])) << 1) | static_cast<char>(falsifies(c[0]));
      if (w == 0 || satisfies(c[0]) || satisfies(c[1])) {
        *cr_ptr2++ = *cr_ptr1++;
        continue;
      }
      assert(w == 1 || w == 2 || w == 3);
      assert(((w & 1) != 0) == falsifies(c[0]));
      assert(((w & 2) != 0) == falsifies(c[1]));

      // find new watched literals if necessary
      for (int k = 2, s = c.size(); w != 0 && k < s; ++k) {
        if (!falsifies(c[k])) {
          const int l = w >> 1;
          assert(falsifies(c[l]));
          const Term fk = c[k].lhs();
          if (fk != f0 && fk != f1 && fk != c[1-l].lhs()) {
            watchers_[fk].push_back(cr);
          }
          assert(std::find(watchers_[fk].begin(), watchers_[fk].end(), cr) != watchers_[fk].end());
          std::swap(c[l], c[k]);
          w = (w - 1) >> 1;  // update w: 11 becomes 01, 10 becomes 00, 01 becomes 00
        }
      }
      if (c[0].lhs() != f && c[1].lhs() != f) {
        ++cr_ptr1;
      }
      assert(((w & 1) != 0) == falsifies(c[0]));
      assert(((w & 2) != 0) == falsifies(c[1]));

      // handle conflicts and/or propagated unit clauses
      if (w == 3) {
        assert(falsifies(c[w >> 1]));
        assert(falsifies(c[1 - (w >> 1)]));
        while (cr_ptr1 != ws.end()) {
          *cr_ptr2++ = *cr_ptr1++;
        }
        trail_head_ = trail_.size();
        conflict = cr;
        assert(std::all_of(c.begin(), c.end(), [this](Literal a) { return falsifies(a); }));
      } else if (w != 0) {
        assert(w == 1 || w == 2);
        assert(falsifies(c[w >> 1]));
        assert(!falsifies(c[1 - (w >> 1)]));
        const Literal b = c[1 - (w >> 1)];
        Enqueue(b, cr);
        assert(std::all_of(c.begin(), c.end(), [this, b](Literal a) { return a == b ? satisfies(a) : falsifies(a); }));
      } else {
        assert(!falsifies(c[0]) && !falsifies(c[1]));
        assert(!falsifies(c[0]) || std::all_of(c.begin()+2, c.end(), [this](Literal a) { return falsifies(a); }));
        assert(!falsifies(c[1]) || std::all_of(c.begin()+2, c.end(), [this](Literal a) { return falsifies(a); }));
      }
    }
    ws.resize(cr_ptr2 - ws.begin());
    return conflict;
  }

  void Analyze(cref_t conflict, std::vector<Literal>* learnt, level_t* btlevel) {
    assert(std::all_of(data_.begin(), data_.end(), [](const auto& ds) { return std::all_of(ds.begin(), ds.end(),
           [](const Data& d) { return !d.seen_subsumed && !d.wanted; }); }));
    int depth = 0;
    Literal trail_a = Literal();
    uref_t trail_i = trail_.size() - 1;
    learnt->push_back(trail_a);

    // see_subsuming(a) marks all literals subsumed by a as seen.
    // By the following reasoning, it suffices to mark only a single literal
    // that implicitly also determines the others as seen.
    // When a literal has been added to the conflict clause, every subsuming
    // literal would be redundant and should be skipped.
    // (1) f == n is only subsumed by f == n.
    // (2) f != n is only subsumed by f != n and f == n' for every n' != n.
    // Every literal we see has a complementary literal on the trail, and the
    // trail does not contain two mutually complementary literals.
    // In case (1), the trail only contains f != n or f == n', but not f == n.
    // Hence we will not see f != n. Therefore marking (f,n) where n !=
    // model_[f] uniquely f == n as seen and nothing else.
    // In case (2), the trail only contains f == n and perhaps f != n', but
    // not f != n or f == n'. Hence we might see f != n and f == n', but not
    // f == n or f != n'. Therefore marking (f,n) where n == model_[f]
    // uniquely identifies f != n and f == n' for all n' != n.
    auto see_subsuming = [this](const Literal a) {
      assert(falsifies(a));
      assert(a.pos() || !model_[a.lhs()].null());
      assert(model_[a.lhs()].null() || (a.pos() != (model_[a.lhs()] == a.rhs())));
      const Term f = a.lhs();
      const Term n = a.rhs();
      data_[f][n].seen_subsumed = 1;
    };
    // seen_subsumed(a) iff some literal subsumed by a has been seen.
    // Some literal subsumed by f == n was seen iff f == n or f != n' was seen for some n'.
    // Some literal subsumed by f != n was seen iff f != n was seen.
    // If f == n was seen, then n != model_[f] and (f,n) is marked.
    // If f != n was seen, then n == model_[f] and (f,model_[f]) is marked.
    auto seen_subsumed = [this](const Literal a) {
      assert(falsifies(a));
      assert(model_[a.lhs()].null() || (a.pos() != (model_[a.lhs()] == a.rhs())));
      const bool p = a.pos();
      const Term f = a.lhs();
      const Term n = a.rhs();
      const Term m = model_[f];
      return data_[f][n].seen_subsumed || (p && !m.null() && data_[f][m].seen_subsumed);
    };

    // want_complementary_on_level(a,l) marks all literals on level l that
    // are complementary to a as wanted.
    // By the following reasoning, it suffices to mark only a single literal
    // that implicitly also determines the others as wanted.
    // When we want a complementary literal to f == n, we prefer f != n over
    // f == model_[f] because this will become f == n in the conflict clause.
    // This also means that we want exactly one literal, which eliminates the
    // need for traversing the whole level again to reset the wanted flag.
    auto want_complementary_on_level = [this](const Literal a, level_t l) {
      assert(falsifies(a));
      assert(data_[a.lhs()][a.rhs()].level <= l);
      const bool p = a.pos();
      const Term f = a.lhs();
      const Term n = a.rhs();
      const Term m = model_[f];
      if (!p) {
        assert(data_[f][n].level == l);
        assert(m == n);
        data_[f][n].wanted = true;
      } else {
        if (data_[f][n].level == l) {
          assert(data_[f][n].model_neq);
          data_[f][n].wanted = true;
        } else {
          assert(!m.null());
          assert(data_[f][m].level == l);
          data_[f][m].wanted = true;
        }
      }
    };
    // wanted_complementary_on_level(a,l) iff a on level l is wanted.
    auto wanted_complementary_on_level = [this](const Literal a, level_t l) {
      assert(falsifies(a));
      assert(data_[a.lhs()][a.rhs()].level <= l);
      const bool p = a.pos();
      const Term f = a.lhs();
      const Term n = a.rhs();
      const Term m = model_[f];
      return !p ?
          data_[f][n].wanted :
          (data_[f][n].level == l && data_[f][n].wanted) || (!m.null() && data_[f][m].wanted);
    };
    // We un-want every trail literal after it has been traversed.
    auto wanted = [this](const Literal a) {
      assert(satisfies(a));
      const Term f = a.lhs();
      const Term n = a.rhs();
      return data_[f][n].wanted;
    };

    auto handle_conflict = [this, &trail_a, &learnt, &depth, &see_subsuming, &seen_subsumed,
                            &want_complementary_on_level, &wanted_complementary_on_level](const Literal a) {
      if (trail_a == a) {
        return;
      }
      assert(falsifies(a));
      assert(!satisfies(a));
      const level_t l = level_of_complementary(a);
      assert(l <= current_level());
      if (l == kRootLevel || seen_subsumed(a) || wanted_complementary_on_level(a, l)) {
        return;
      }
      if (l < current_level()) {
        learnt->push_back(a);
        see_subsuming(a);
        Bump(a.lhs());
      } else if (l == current_level()) {
        ++depth;
        want_complementary_on_level(a, l);
        Bump(a.lhs());
      }
    };

    do {
      assert(conflict != kNullRef);
      if (conflict == kDomainRef) {
        assert(!trail_a.null());
        assert(trail_a.pos());
        const Term f = trail_a.lhs();
        for (const Term n : names_[f.sort()]) {
          if (!n.null() && data_[f][n].occurs) {
            const Literal a = Literal::Eq(f, n);
            handle_conflict(a);
          }
        }
      } else {
        for (const Literal a : clause(conflict)) {
          handle_conflict(a);
        }
      }
      assert(depth > 0);

      while (!wanted(trail_[trail_i--])) {
        assert(trail_i >= 0);
      }
      trail_a = trail_[trail_i + 1];
      data_[trail_a.lhs()][trail_a.rhs()].wanted = false;
      --depth;
      conflict = reason_of(trail_a);
    } while (depth > 0);
    (*learnt)[0] = trail_a.flip();

    for (const Literal a : (*learnt)) {
      data_[a.lhs()][a.rhs()].seen_subsumed = false;
    }

    learnt->resize(Clause::Normalize<Clause::kGuaranteeInvalid>(learnt->size(), learnt->data()));

    if (learnt->size() == 1) {
      *btlevel = kRootLevel;
    } else {
      assert(learnt->size() >= 2);
      uref_t max = 1;
      *btlevel = level_of_complementary((*learnt)[max]);
      for (uref_t i = 2, s = learnt->size(); i != s; ++i) {
        level_t l = level_of_complementary((*learnt)[i]);
        if (*btlevel < l) {
          max = i;
          *btlevel = l;
        }
      }
      std::swap((*learnt)[1], (*learnt)[max]);
    }
    assert(level_of(trail_a) > *btlevel && *btlevel >= kRootLevel);
    assert(std::all_of(learnt->begin(), learnt->end(), [this](Literal a) { return falsifies(a); }));
    assert(std::all_of(learnt->begin(), learnt->end(), [this](Literal a) { return !satisfies(a); }));
    assert(std::all_of(data_.begin(), data_.end(), [](const auto& ds) { return std::all_of(ds.begin(), ds.end(), [](const Data& d) { return !d.seen_subsumed; }); }));
    assert(std::all_of(data_.begin(), data_.end(), [](const auto& ds) { return std::all_of(ds.begin(), ds.end(), [](const Data& d) { return !d.wanted; }); }));
  }

  void NewLevel() { level_size_.push_back(trail_.size()); }

  void Enqueue(const Literal a, const cref_t reason) {
    assert(a.primitive());
    assert(data_[a.lhs()][a.rhs()].occurs);
    const bool p = a.pos();
    const Term f = a.lhs();
    const Term n = a.rhs();
    const Term m = model_[f];
    if (m.null() && (p || !data_[f][n].model_neq)) {
      assert(domain_size_[a.lhs()] >= 1 + !a.pos());
      assert(!satisfies(a));
      trail_.push_back(a);
      data_[f][n].Update(!p, current_level(), reason);
      if (p) {
        model_[f] = n;
        order_.Remove(f);
      } else if (--domain_size_[f] == 1) {
        const Term n = CandidateName(f);
        trail_.push_back(Literal::Eq(f, n));
        data_[f][n].Update(false, current_level(), kDomainRef);
        model_[f] = n;
        order_.Remove(f);
        assert(satisfies(Literal::Eq(f, n)));
      } else {
        BumpToFront(f);
      }
    }
    assert(satisfies(a));
  }

  void Backtrack(const level_t l) {
    for (auto a = trail_.cbegin() + level_size_[l], e = trail_.cend(); a != e; ++a) {
      const bool p = a->pos();
      const Term f = a->lhs();
      const Term n = a->rhs();
      model_[f] = Term();
      if (p) {
        if (!data_[f][n].model_neq) {
          data_[f][n].Reset();
        }
        order_.Insert(f);
      } else {
        data_[f][n].Reset();
        ++domain_size_[f];
      }
    }
    trail_.resize(level_size_[l]);
    level_size_.resize(l);
    trail_head_ = trail_.size();
  }

  Term CandidateName(Term f) {
    assert(!f.null() && model_[f].null());
    const DenseSet<Term>& names = names_[f.sort()];
    const int size = names.upper_bound();
    const int offset = name_index_[f];
#if 1
    for (int i = offset; i >= 0; --i) {
      const Term n = names[i];
      if (!n.null() && data_[f][n].occurs && !data_[f][n].model_neq) {
        name_index_[f] = i;
        return n;
      }
    }
    for (int i = size - 1; i > offset; --i) {
      const Term n = names[i];
      if (!n.null() && data_[f][n].occurs && !data_[f][n].model_neq) {
        name_index_[f] = i;
        return n;
      }
    }
#else
    int i = 0;
    const int end = i;
    do {
      std::cout << i << " ";
      const Term n = names[i];
      if (!n.null() && data_[f][n].occurs && !data_[f][n].model_neq) {
        name_index_[f] = i;
        std::cout << std::endl;
        return n;
      }
      ++i;
      i %= size;
    } while (i != end);
#endif
    return Term();
  }

  void BumpToFront(Term f) {
    for (const double& activity : activity_) {
      if (activity_[f] < activity) {
        activity_[f] = activity;
      }
    }
    activity_[f] += bump_step_;
    if (order_.Contains(f)) {
      order_.Increase(f);
    }
  }

  void Bump(Term f) {
    activity_[f] += bump_step_;
    if (activity_[f] > 1e100) {
      for (double& activity : activity_) {
        activity *= 1e-100;
      }
      bump_step_ *= 1e-100;
    }
    if (order_.Contains(f)) {
      order_.Increase(f);
    }
  }

  bool satisfies(const Literal a, const level_t l = -1) const {
    const bool p = a.pos();
    const Term f = a.lhs();
    const Term n = a.rhs();
    const Term m = model_[f];
    return ((p && m == n) ||
            (!p && ((!m.null() && m != n) || data_[f][n].model_neq))) &&
           (l < 0 || data_[f][n].level <= l);
  }

  bool falsifies(const Literal a, const level_t l = -1) const {
    const bool p = a.pos();
    const Term f = a.lhs();
    const Term n = a.rhs();
    const Term m = model_[f];
    return ((!p && m == n) ||
            (p && ((!m.null() && m != n) || data_[f][n].model_neq))) &&
           (l < 0 || data_[f][n].level <= l);
  }

  bool satisfies(const Clause& c, const level_t l = -1) const {
    return std::any_of(c.begin(), c.end(), [this, l](Literal a) { return satisfies(a, l); });
  }

  bool falsifies(const Clause& c, const level_t l = -1) const {
    return std::all_of(c.begin(), c.end(), [this, l](Literal a) { return falsifies(a, l); });
  }

  level_t level_of(Literal a) const {
    assert(a.primitive());
    assert(satisfies(a));
    assert(!a.pos() || model_[a.lhs()] == a.rhs());
    const bool p = a.pos();
    const Term f = a.lhs();
    const Term n = a.rhs();
    const Term m = model_[f];
    return !p && data_[f][n].model_neq ? data_[f][n].level : data_[f][m].level;
  }

  level_t level_of_complementary(Literal a) const {
    assert(a.primitive());
    assert(falsifies(a));
    assert(a.pos() || model_[a.lhs()] == a.rhs());
    const bool p = a.pos();
    const Term f = a.lhs();
    const Term n = a.rhs();
    const Term m = model_[f];
    return p && data_[f][n].model_neq ? data_[f][n].level : data_[f][m].level;
  }

  cref_t reason_of(Literal a) const {
    assert(a.primitive());
    assert(satisfies(a));
    assert(!a.pos() || model_[a.lhs()] == a.rhs());
    const bool p = a.pos();
    const Term f = a.lhs();
    const Term n = a.rhs();
    const Term m = model_[f];
    return !p && data_[f][n].model_neq ? data_[f][n].reason : data_[f][m].reason;
  }

  level_t current_level() const { return level_size_.size(); }

  void CapacitateMaps(const Symbol::Sort s, const Term f, const Term n, const Term extra_n) {
    const int max_ni = n.index() > extra_n.index() ? n.index() : extra_n.index();
    const int si = s.index() >= names_.upper_bound() ? s.index() : -1;
    const int fi = f.index() >= funcs_.upper_bound() ? f.index() : -1;
    const int ni = names_.upper_bound() == 0 || max_ni >= names_[0].upper_bound() ? max_ni : -1;
    const int sig = (si + 1) * 1.5;
    const int fig = (fi + 1) * 1.5;
    const int nig = ni >= 0 ? (ni + 1) * 1.5 : names_[0].upper_bound();
    if (fi >= 0) {
      funcs_.Capacitate(fig);
      name_index_.Capacitate(fig);
      watchers_.Capacitate(fig);
      model_.Capacitate(fig);
      data_.Capacitate(fig);
      domain_size_.Capacitate(fig);
      order_.Capacitate(fig);
      activity_.Capacitate(fig);
    }
    if (si >= 0) {
      names_.Capacitate(sig);
      name_extra_.Capacitate(sig);
    }
    if (fi >= 0 || ni >= 0) {
      for (DenseMap<Term, Data>& ds : data_) {
        ds.Capacitate(nig);
      }
    }
    if (si >= 0 || ni >= 0) {
      for (DenseSet<Term>& ns : names_) {
        ns.Capacitate(nig);
      }
      for (DenseMap<Term, Data>& ds : data_) {
        ds.Capacitate(nig);
      }
    }
  }

  template<typename T>
  static void SwapOut(std::vector<T>* vec, int i) {
    int s = vec->size();
    --s;
    std::swap((*vec)[i], (*vec)[s]);
    vec->resize(s);
  }

  // empty_clause_ is true iff the empty clause has been derived.
  bool empty_clause_ = false;

  // clauses_ is the sequence of clauses added initially or learnt.
  Clause::Factory clause_factory_;
  std::vector<cref_t> clauses_ = std::vector<cref_t>(1, 0);

  // funcs_ is the set of functions that occur in clauses.
  // names_ is the set of names that occur in clauses plus extra names.
  // name_extra_ stores an additional name for every sort.
  DenseSet<Term>                         funcs_;
  DenseMap<Symbol::Sort, DenseSet<Term>> names_;
  DenseMap<Symbol::Sort, Term>           name_extra_;
  DenseMap<Term, int>                    name_index_;

  // watchers_ maps every function to a sequence of clauses that watch it.
  // Every clause watches two functions, and when a literal with this function
  // is propagated, the watching clauses are inspected.
  DenseMap<Term, std::vector<cref_t>> watchers_;

  // trail_ is a sequence of literals in the order they were derived.
  // level_size_ groups the literals of trail_ into chunks by their level at
  // which they were derived, where level_size_[l] determines the number of
  // literals set or derived up to level l.
  // trail_head_ is the index of the first literal of trail_ that hasn't been
  // propagated yet.
  std::vector<Literal> trail_;
  std::vector<uref_t>  level_size_ = std::vector<uref_t>(1, trail_.size());
  uref_t               trail_head_ = 0;

  // model_ is an assignment of functions to names, i.e., positive literals.
  // data_ is meta data for every function and name pair (cf. Data).
  // domain_size_ is the number of candidates for every function.
  DenseMap<Term, Term>                 model_;
  DenseMap<Term, DenseMap<Term, Data>> data_;
  DenseMap<Term, int>                  domain_size_;

  // order_ is a heap that ranks functions by their activity.
  // activity_ stores the activity of each activity.
  // bump_step_ is the increase factor after .
  Heap<Term, ActivityCompare> order_{ActivityCompare(&activity_)};
  DenseMap<Term, double>      activity_;
  double                      bump_step_ = 1.0;
};

}  // namespace limbo

#endif  // LIMBO_SOLVER_H_

