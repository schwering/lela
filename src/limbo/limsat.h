// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2019 Christoph Schwering
// Licensed under the MIT license. See LICENSE file in the project root.
//
// Limited satisfiability solver that checks if for all functions f_1, ..., f_k,
// there are names n_1, ..., n_k such that the partial model obtained by closing
// f_1 = n_1, ..., f_k = n_k under unit propagation with the clauses satisfies
// all those clauses and does not satisfy the query.
//
// Provided the NNF of the query does not contain valid subclauses, the above
// statement is equivalent to: for all f_1, ..., f_k, for some n_1, ..., n_k,
// there is a multi-valued world that satisfies all clauses closed under unit
// propagation with f_1 = n_1, ..., f_k = n_k and is consistent for all units,
// and falsifies the clause.
//
// This formulation in turn is the negation of the (new) semantics of limited
// belief.

#ifndef LIMBO_LIMSAT_H_
#define LIMBO_LIMSAT_H_

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

#include <limbo/formula.h>
#include <limbo/sat.h>
#include <limbo/internal/dense.h>
#include <limbo/internal/subsets.h>

#include <limbo/io/output.h>

namespace limbo {

class LimSat {
 public:
  using LitVec = std::vector<Lit>;

  explicit LimSat() = default;

  LimSat(const LimSat&)            = delete;
  LimSat& operator=(const LimSat&) = delete;
  LimSat(LimSat&&)                 = default;
  LimSat& operator=(LimSat&&)      = default;

  bool AddClause(const LitVec& as) {
    LitVec copy = as;
    return AddClause(std::move(copy));
  }

  bool AddClause(LitVec&& as) {
    std::sort(as.begin(), as.end());
    auto p = clauses_.insert(std::move(as));
    if (p.second) {
      clauses_vec_.push_back(*p.first);
      //clauses_not_yet_added_.push_back(p.first);
      for (const Lit a : *p.first) {
        const Fun f = a.fun();
        const Name n = a.name();
        domains_.FitForKey(f);
        domains_[f].FitForKey(n);
        domains_[f][n] = true;
        extra_name_id_ = std::max(int(n) + 1, extra_name_id_);
        if (!sat_.registered(f, n)) {
          assert(!extra_name_registered_);
          sat_.Register(f, n);
        }
      }
    }
    return p.second;
  }

  const std::set<LitVec>& clauses() const { return clauses_; }

  bool Solve(int k, const RFormula& query) { return FindModels(k, query); }

 private:
  enum class SolverType { kWithLearntClauses, kWithoutLearntClauses };

  struct FoundModel {
    FoundModel() : succ(false) {}
    FoundModel(TermMap<Fun, Name>&& model) : model(model), succ(true) {}
    FoundModel(const TermMap<Fun, Name>& model) : model(model), succ(true) {}
    TermMap<Fun, Name> model;
    bool succ;
  };

  struct FoundCoveringModels {
    FoundCoveringModels() : all_covered(false) {}
    FoundCoveringModels(std::vector<TermMap<Fun, Name>>&& models,
                        std::vector<std::vector<Fun>>&& newly_assigned_in)
        : models(models), newly_assigned_in(newly_assigned_in), all_covered(true) {}
    std::vector<TermMap<Fun, Name>> models;
    std::vector<std::vector<Fun>> newly_assigned_in;
    bool all_covered;
  };

  struct AssignedFunctions {
    AssignedFunctions(std::vector<Fun>&& newly_assigned, bool all_assigned)
        : newly_assigned(newly_assigned), all_assigned(all_assigned) {}
    std::vector<Fun> newly_assigned;
    bool all_assigned = false;
  };

  static bool assigns(const TermMap<Fun, Name>& model, const Fun f) {
    return model.key_in_range(f) && !model[f].null();
  }

  static bool AssignsAll(const TermMap<Fun, Name>& model, const std::vector<Fun>& funs) {
    for (const Fun f : funs) {
      if (!assigns(model, f)) {
        return false;
      }
    }
    return true;
  }

  static bool AssignsAll(const TermMap<Fun, Name>& model, const TermMap<Fun, bool>& wanted) {
    for (const Fun f : wanted.keys()) {
      if (wanted[f] && !assigns(model, f)) {
        return false;
      }
    }
    return true;
  }

  template<typename T>
  static std::vector<Fun> Merge(const std::vector<T>& xs, const std::vector<Fun>& ys) {
    std::vector<Fun> zs;
    std::set_union(xs.begin(), xs.end(), ys.begin(), ys.end(), std::back_inserter(zs));
    return zs;
  }

  static AssignedFunctions GetAndUnwantNewlyAssignedFunctions(const TermMap<Fun, Name> model,
                                                              TermMap<Fun, bool>* wanted) {
    std::vector<Fun> newly_assigned;
    bool all_assigned = true;
    for (const Fun f : wanted->keys()) {
      if ((*wanted)[f]) {
        if (assigns(model, f)) {
          (*wanted)[f] = false;
          newly_assigned.push_back(f);
        } else {
          all_assigned = false;
        }
      }
    }
    return AssignedFunctions(std::move(newly_assigned), all_assigned);
  }

  bool FindModels(const int min_model_size, const RFormula& query) {
    //printf("FindModels:%d\n", __LINE__);
    UpdateDomainsForQuery(query);
    // Find models such that every function is assigned a value in some model.
    // For example, consider a problem with functions 1,2,3,4,5 and minimum
    // model size 2.
    // We might find two models M1 and M2 that assign 1,2,3 and 3,4,5, which
    // covers all functions. M1 and M2 imply models that assign the subsets of
    // cardinality of size 2 of {1,2,3} and {3,4,5}, that is,
    // {1,2}, {2,3}, {1,3}, and {3,4}, {4,5}, {3,5}.
    const FoundCoveringModels fcm = FindCoveringModels(min_model_size, query);
    if (!fcm.all_covered) {
      //std::cout << __FILE__ << ":" << __LINE__ << " " << query << std::endl;
      return false;
    }
    // Now find models for sets for which models aren't implied yet.
    // In the example, the sets {{x,y} | x in {1,2,3}, y in {4,5}} that are not
    // subsets of {1,2,3} or {3,4,5}.
    //std::cout << __FILE__ << ":" << __LINE__ << " " << query << std::endl;
    //std::cout << __FILE__ << ":" << __LINE__ << " min_model_size=" << min_model_size << std::endl;
    //std::cout << __FILE__ << ":" << __LINE__ << " fcm.newly_assigned_in=" << fcm.newly_assigned_in << std::endl;
    return internal::AllCombinedSubsetsOfSize(fcm.newly_assigned_in, min_model_size,
                                              [&](const std::vector<Fun>& must) -> bool {
      // Skip sets of functions that have been covered already.
      // In the example, {3,4} and {3,5} are implied by M2.
      for (const TermMap<Fun, Name>& model : fcm.models) {
        if (AssignsAll(model, must)) {
          //std::cout << __FILE__ << ":" << __LINE__ << " " << query << std::endl;
          return true;
        }
      }
      TermMap<Fun, bool> wanted;
      wanted.FitForKey(domains_.upper_bound_key(), false);
      for (const Fun f : must) {
        wanted[f] = true;
      }
      constexpr bool propagate_with_learnt = false;
      constexpr bool wanted_is_must = true;
      const FoundModel fm = FindModel(min_model_size, query, propagate_with_learnt, wanted_is_must, wanted);
      //std::cout << __FILE__ << ":" << __LINE__ << " " << query << " " << fm.succ << std::endl;
      return fm.succ;
    });
  }

  FoundCoveringModels FindCoveringModels(const int min_model_size, const RFormula& query) {
    std::vector<TermMap<Fun, Name>> models;
    std::vector<std::vector<Fun>> newly_assigned_in;
    TermMap<Fun, bool> wanted;
    wanted.FitForKey(domains_.upper_bound_key());
    for (const Fun f : domains_.keys()) {
      wanted[f] = !domains_[f].empty();
    }
    bool propagate_with_learnt = true;
    bool wanted_is_must = false;
    for (;;) {
      //printf("FindCoveringModels:%d\n", __LINE__);
      const FoundModel fm = FindModel(min_model_size, query, propagate_with_learnt, wanted_is_must, wanted);
      if (!fm.succ && propagate_with_learnt) {
        //std::cout << __FILE__ << ":" << __LINE__ << " " << query << " | " << query.head().u.a.fun() << " = " << (fm.model.key_in_range(query.head().u.a.fun()) ? fm.model[query.head().u.a.fun()] : Name()) << std::endl;
        propagate_with_learnt = false;
        continue;
      }
      if (!fm.succ) {
        //std::cout << __FILE__ << ":" << __LINE__ << " " << query << " | " << query.head().u.a.fun() << " = " << (fm.model.key_in_range(query.head().u.a.fun()) ? fm.model[query.head().u.a.fun()] : Name()) << std::endl;
        return FoundCoveringModels();
      }
      if (min_model_size == 0) {
        //std::cout << __FILE__ << ":" << __LINE__ << " " << query << " | " << query.head().u.a.fun() << " = " << (fm.model.key_in_range(query.head().u.a.fun()) ? fm.model[query.head().u.a.fun()] : Name()) << std::endl;
        return FoundCoveringModels(std::move(models), std::move(newly_assigned_in));
      }
      AssignedFunctions gaf = GetAndUnwantNewlyAssignedFunctions(fm.model, &wanted);
      if (gaf.newly_assigned.empty() && !wanted_is_must) {
        //std::cout << __FILE__ << ":" << __LINE__ << " " << query << " | " << query.head().u.a.fun() << " = " << (fm.model.key_in_range(query.head().u.a.fun()) ? fm.model[query.head().u.a.fun()] : Name()) << std::endl;
        wanted_is_must = true;
        continue;
      }
      // Remove previous models that assign a subset of the newly found model.
      for (int i = 0; i < int(models.size()); ) {
        if (AssignsAll(fm.model, newly_assigned_in[i])) {
          gaf.newly_assigned = Merge(gaf.newly_assigned, newly_assigned_in[i]);
          models.erase(models.begin() + i);
          newly_assigned_in.erase(newly_assigned_in.begin() + i);
        } else {
          ++i;
        }
      }
      models.push_back(fm.model);
      newly_assigned_in.push_back(gaf.newly_assigned);
      if (gaf.all_assigned) {
        //std::cout << __FILE__ << ":" << __LINE__ << " " << query << " | " << query.head().u.a.fun() << " = " << (fm.model.key_in_range(query.head().u.a.fun()) ? fm.model[query.head().u.a.fun()] : Name()) << std::endl;
        return FoundCoveringModels(std::move(models), std::move(newly_assigned_in));
      }
    }
  }

  FoundModel FindModel(const int min_model_size,
                       const RFormula& query,
                       const bool propagate_with_learnt,
                       const bool wanted_is_must,
                       const TermMap<Fun, bool>& wanted) {
    //printf("FindModel: min_model_size = %d, propagate_with_learnt = %s, wanted_is_must = %s, wanted =", min_model_size, propagate_with_learnt ? "true" : "false", wanted_is_must ? "true" : "false"); for (Fun f : wanted.keys()) { if (wanted[f]) { printf(" %d", int(f)); } } printf("\n");
    auto activity = [&wanted](Fun f) { return wanted.key_in_range(f) ? wanted[f] * kActivityOffset : 0.0; };
#if 1
    InitSat(activity);
#else
    sat_ = Sat();
    for (const auto& c : clauses_vec_) {
      for (const Lit a : c) {
        sat_.Register(a.fun(), a.name(), activity);
      }
    }
    sat_.RegisterExtraName(Name::FromId(extra_name_id_));
    for (const auto& c : clauses_vec_) {
      sat_.AddClause(c);
    }
    //sat_.Reset(Sat::KeepLearnt(propagate_with_learnt), activity);
#endif
    sat_.set_propagate_with_learnt(propagate_with_learnt);
    TermMap<Fun, Name> partial_model;
    int partial_model_size = -1;
    int n_conflicts = 0;
    const Sat::Truth truth = sat_.Solve(
        [&](int, Sat::CRef, const LitVec&, int) -> bool {
          return ++n_conflicts <= kMaxConflicts;
        },
        [&](int, Lit) -> bool {
          if (min_model_size <= sat_.model_size() && partial_model_size < sat_.model_size() &&
              (!wanted_is_must || AssignsAll(sat_.model(), wanted)) && !query.SatisfiedBy(partial_model, nullptr)) {
            partial_model_size = sat_.model_size();
            partial_model = sat_.model();
          }
          return true;
        },
        [&](const TermMap<Fun, Name>& model, LitVec* nogood) {
          const bool sat = query.SatisfiedBy(model, nogood);
          if (!sat && min_model_size <= sat_.model_size() && partial_model_size < sat_.model_size() &&
              (!wanted_is_must || AssignsAll(sat_.model(), wanted))) {
            partial_model_size = sat_.model_size();
            partial_model = sat_.model();
          }
          return sat;
        });
    if (truth == Sat::Truth::kSat) {
      //printf("FindModel %d: true, partial_model_size = %d, assignment =", __LINE__, sat_.model_size()); for (const Fun f : sat_.model().keys()) { if (assigns(sat_.model(), f)) { printf(" (%d = %d)", int(f), int(sat_.model()[f])); } } printf("\n");
      assert(AssignsAll(sat_.model(), wanted));
      return FoundModel(sat_.model());
    } else if (partial_model_size >= min_model_size && !query.SatisfiedBy(partial_model, nullptr)) {
      //printf("FindModel %d: true, partial_model_size = %d, assignment =", __LINE__, partial_model_size); for (const Fun f : partial_model.keys()) { if (assigns(partial_model, f)) { printf(" (%d = %d)", int(f), int(partial_model[f])); } } printf("\n");
      return FoundModel(std::move(partial_model));
    } else {
      //printf("FindModel %d: false\n", __LINE__);
      return FoundModel();
    }
  }

  void UpdateDomainsForQuery(const RFormula& query) {
    for (const Alphabet::Symbol& s : query) {
      if (s.tag == Alphabet::Symbol::kStrippedLit) {
        const Fun f = s.u.a.fun();
        const Name n = s.u.a.name();
        domains_.FitForKey(f);
        domains_[f].FitForKey(n, false);
        if (!domains_[f][n]) {
          domains_[f][n] = true;
          extra_name_id_ = std::max(int(n) + 1, extra_name_id_);
          sat_.Register(f, n);
        }
      } else {
        assert(!s.stripped());
      }
    }
  }

  template<typename ActivityFunction>
  void InitSat(ActivityFunction activity = ActivityFunction()) {
    if (!extra_name_registered_) {
      sat_.RegisterExtraName(Name::FromId(extra_name_id_));
      extra_name_registered_ = true;
    }
    sat_.Reset(Sat::KeepLearnt(false), activity);
    for (; sat_init_index_ < int(clauses_vec_.size()); ++sat_init_index_) {
      sat_.AddClause(clauses_vec_[sat_init_index_]);
    }
  }

  static constexpr double kActivityOffset = 1000.0;
  static constexpr int    kMaxConflicts   = 50;

  std::set<LitVec>                              clauses_;
  std::vector<LitVec>                           clauses_vec_;
  //std::vector<std::set<LitVec>::const_iterator> clauses_not_yet_added_;

  TermMap<Fun, TermMap<Name, bool>> domains_;
  bool                              extra_name_registered_ = false;
  int                               extra_name_id_ = 1;

  Sat sat_;
  int sat_init_index_ = 0;
};

}  // namespace limbo

#endif  // LIMBO_LIMSAT_H_

