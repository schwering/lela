// vim:filetype=cpp:textwidth=80:shiftwidth=2:softtabstop=2:expandtab
// schwering@kbsg.rwth-aachen.de

#ifndef _TERM_H_
#define _TERM_H_

class Term {
 public:
  typedef int VarId;
  typedef int NameId;

  static Term CreateVariable(VarId id);
  static Term CreateStdName(NameId id);

  Term();

  bool operator==(const Term& t) const;
  bool operator<(const Term& t) const;

  inline bool is_variable() const { return type_ == VAR; }
  inline bool is_name() const { return type_ == NAME; }
  inline bool is_ground() const { return type_ == VAR; }

 private:
  enum Type { DUMMY, VAR, NAME };

  Term(Type type, int id);

  Type type_;
  int id_;
};

#endif

