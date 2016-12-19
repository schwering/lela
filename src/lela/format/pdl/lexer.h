// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2016 Christoph Schwering
//
// Max-munch lexer for the problem description language.
//
// The compuational complexity is pretty bad (O(n^2)), but we don't expect very
// long tokens, so parsing shouldn't be the bottleneck.

#ifndef LELA_FORMAT_PDL_LEXER_H_
#define LELA_FORMAT_PDL_LEXER_H_

#include <cassert>

#include <algorithm>
#include <functional>
#include <list>
#include <string>
#include <utility>

#include <lela/internal/iter.h>

namespace lela {
namespace format {
namespace pdl {

class Token {
 public:
  enum Id { kError, kSort, kVar, kName, kFun, kKB, kLet, kEntails, kConsistent, kAssert, kRefute, kColon, kComma,
    kEndOfLine, kEquality, kInequality, kNot, kOr, kAnd, kForall, kExists, kAssign, kRArrow, kLRArrow, kSlash, kComment,
    kLeftParen, kRightParen, kUint, kIdentifier };

  Token() : id_(kError) {}
  explicit Token(Id id) : id_(id) {}
  Token(Id id, const std::string& str) : id_(id), str_(str) {}

  Id id() const { return id_; }
  const std::string& str() const { return str_; }

 private:
  friend std::ostream& operator<<(std::ostream& os, const Token& t);

  Id id_;
  std::string str_;
};

template<typename ForwardIt>
class Lexer {
 private:
  class Word {
   public:
    Word(ForwardIt begin, ForwardIt end) : begin_(begin), end_(end) {}
    ForwardIt begin() const { return begin_; }
    ForwardIt end() const { return end_; }
    std::string str() const { return std::string(begin_, end_); }
   private:
    ForwardIt begin_;
    ForwardIt end_;
  };

 public:
  enum Match { kMismatch, kPrefixMatch, kFullMatch };
  typedef std::list<std::pair<Token::Id, std::function<Match(Word)>>> LexemeVector;

  struct iterator {
    typedef std::ptrdiff_t difference_type;
    typedef Token value_type;
    typedef value_type reference;
    typedef value_type* pointer;
    typedef std::forward_iterator_tag iterator_category;
    typedef lela::internal::iterator_proxy<iterator> proxy;

    iterator() = default;
    iterator(const LexemeVector* lexemes, ForwardIt it, ForwardIt end) : lexemes_(lexemes), it_(it), end_(end) {
      SkipToNext();
    }

    bool operator==(const iterator& it) const { return it_ == it.it_ && end_ == it.end_; }
    bool operator!=(const iterator& it) const { return !(*this == it); }

    reference operator*() const {
      const Word w = CurrentWord();
      const std::pair<Match, Token::Id> m = LexemeMatch(w);
      return Token(m.second, w.str());
    }

    iterator& operator++() {
      it_ = CurrentWord().end();
      SkipToNext();
      return *this;
    }

    proxy operator->() const { return proxy(operator*()); }
    proxy operator++(int) { proxy p(operator*()); operator++(); return p; }

    ForwardIt char_iter() const { return it_; }

   private:
    template<typename UnaryPredicate>
    void SkipWhile(UnaryPredicate p) {
      for (; it_ != end_ && p(*it_); ++it_) {
      }
    }

    void SkipToNext() {
      SkipWhile([](char c) { return IsWhitespace(c); });
      while (it_ != end_ && LexemeMatch(CurrentWord()).second == Token::kComment) {
        SkipWhile([](char c) { return !IsNewLine(c); });
        SkipWhile([](char c) { return IsWhitespace(c); });
      }
    }

    Word CurrentWord() const {
      // Max-munch lexer
      assert(it_ != end_);
      ForwardIt it = it_;
      ForwardIt jt;
      std::pair<Match, Token::Id> m;
      for (jt = it; jt != end_; ++jt) {
        m = LexemeMatch(Word(it, std::next(jt)));
        if (m.first == kMismatch) {
          break;
        }
      }
      assert(it != jt);
      return Word(it, jt);
    }

    std::pair<Match, Token::Id> LexemeMatch(Word w) const {
      Match best_match = kMismatch;
      Token::Id best_token = Token::kError;
      for (auto& p : *lexemes_) {
        const Match m = p.second(w);
        if ((best_match == kMismatch && m == kPrefixMatch) || (best_match != kFullMatch && m == kFullMatch)) {
          best_match = m;
          best_token = p.first;
        }
      }
      return std::make_pair(best_match, best_token);
    }

    const LexemeVector* lexemes_;
    ForwardIt it_;
    ForwardIt end_;
  };

  Lexer(ForwardIt begin, ForwardIt end) : begin_(begin), end_(end) {
    lexemes_.emplace_back(Token::kSort,       [](Word w) { return IsPrefix(w, "sort"); });
    lexemes_.emplace_back(Token::kVar,        [](Word w) { return IsPrefix(w, {"var", "variable"}); });
    lexemes_.emplace_back(Token::kName,       [](Word w) { return IsPrefix(w, {"name", "stdname"}); });
    lexemes_.emplace_back(Token::kFun,        [](Word w) { return IsPrefix(w, {"fun", "function"}); });
    lexemes_.emplace_back(Token::kKB,         [](Word w) { return IsPrefix(w, "kb"); });
    lexemes_.emplace_back(Token::kLet,        [](Word w) { return IsPrefix(w, "let"); });
    lexemes_.emplace_back(Token::kEntails,    [](Word w) { return IsPrefix(w, "entails"); });
    lexemes_.emplace_back(Token::kConsistent, [](Word w) { return IsPrefix(w, "consistent"); });
    lexemes_.emplace_back(Token::kAssert,     [](Word w) { return IsPrefix(w, "assert"); });
    lexemes_.emplace_back(Token::kRefute,     [](Word w) { return IsPrefix(w, "refute"); });
    lexemes_.emplace_back(Token::kColon,      [](Word w) { return IsPrefix(w, ":"); });
    lexemes_.emplace_back(Token::kEndOfLine,  [](Word w) { return IsPrefix(w, ";"); });
    lexemes_.emplace_back(Token::kComma,      [](Word w) { return IsPrefix(w, ","); });
    lexemes_.emplace_back(Token::kEquality,   [](Word w) { return IsPrefix(w, {"==", "="}); });
    lexemes_.emplace_back(Token::kInequality, [](Word w) { return IsPrefix(w, {"!=", "/="}); });
    lexemes_.emplace_back(Token::kNot,        [](Word w) { return IsPrefix(w, {"!", "~"}); });
    lexemes_.emplace_back(Token::kOr,         [](Word w) { return IsPrefix(w, {"||", "|", "v"}); });
    lexemes_.emplace_back(Token::kAnd,        [](Word w) { return IsPrefix(w, {"&&", "&", "^"}); });
    lexemes_.emplace_back(Token::kForall,     [](Word w) { return IsPrefix(w, "fa"); });
    lexemes_.emplace_back(Token::kExists,     [](Word w) { return IsPrefix(w, "ex"); });
    lexemes_.emplace_back(Token::kAssign,     [](Word w) { return IsPrefix(w, ":="); });
    lexemes_.emplace_back(Token::kRArrow,     [](Word w) { return IsPrefix(w, "->"); });
    lexemes_.emplace_back(Token::kLRArrow,    [](Word w) { return IsPrefix(w, "<->"); });
    lexemes_.emplace_back(Token::kSlash,      [](Word w) { return IsPrefix(w, "/"); });
    lexemes_.emplace_back(Token::kComment,    [](Word w) { return IsPrefix(w, "//"); });
    lexemes_.emplace_back(Token::kLeftParen,  [](Word w) { return IsPrefix(w, "("); });
    lexemes_.emplace_back(Token::kRightParen, [](Word w) { return IsPrefix(w, ")"); });
    lexemes_.emplace_back(Token::kUint,
                          [](Word w) { return w.begin() == w.end()
                                                  ? kPrefixMatch :
                                              (*w.begin() != '0' || std::next(w.begin()) == w.end()) &&
                                               std::all_of(w.begin(), w.end(), [](char c) { return IsDigit(c); })
                                                  ? kFullMatch
                                                  : kMismatch; });
    lexemes_.emplace_back(Token::kIdentifier,
                          [](Word w) { return w.begin() == w.end()
                                                  ? kPrefixMatch :
                                              IsAlpha(*w.begin()) &&
                                              std::all_of(w.begin(), w.end(), [](char c) { return IsAlnum(c); })
                                                  ? kFullMatch
                                                  : kMismatch; });
  }

  iterator begin() const { return iterator(&lexemes_, begin_, end_); }
  iterator end()   const { return iterator(&lexemes_, end_, end_); }

 private:
  static_assert(std::is_convertible<typename ForwardIt::iterator_category, std::forward_iterator_tag>::value,
                "ForwardIt has wrong iterator category");

  static Match IsPrefix(Word w, const std::string& sentence) {
    size_t len = std::distance(w.begin(), w.end());
    auto r = lela::internal::transform_range(w.begin(), w.end(), [](char c) { return std::tolower(c); });
    if (len > sentence.length() || std::mismatch(r.begin(), r.end(), sentence.begin()).first != r.end()) {
      return kMismatch;
    } else {
      return len < sentence.length() ? kPrefixMatch : kFullMatch;
    }
  }

  static Match IsPrefix(Word w, const std::initializer_list<std::string>& sentences) {
    Match m = kMismatch;
    for (const auto& sentence : sentences) {
      m = std::max(m, IsPrefix(w, sentence));
    }
    return m;
  }

  static bool IsNewLine(char c) { return c == '\r' || c == '\n'; }
  static bool IsWhitespace(char c) { return c == ' ' || c == '\t' || IsNewLine(c); }
  static bool IsDigit(char c) { return '0' <= c && c <= '9'; }
  static bool IsAlpha(char c) { return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '_'; }
  static bool IsAlnum(char c) { return IsAlpha(c) || IsDigit(c) || c == '-'; }

  LexemeVector lexemes_;
  ForwardIt begin_;
  ForwardIt end_;
};

std::ostream& operator<<(std::ostream& os, Token::Id t) {
  switch (t) {
    case Token::kSort:       return os << "kSort";
    case Token::kVar:        return os << "kVar";
    case Token::kName:       return os << "kName";
    case Token::kFun:        return os << "kFun";
    case Token::kKB:         return os << "kKB";
    case Token::kLet:        return os << "kLet";
    case Token::kEntails:    return os << "kEntails";
    case Token::kConsistent: return os << "kConsistent";
    case Token::kAssert:     return os << "kAssert";
    case Token::kRefute:     return os << "kRefute";
    case Token::kColon:      return os << "kColon";
    case Token::kEndOfLine:  return os << "kEndOfLine";
    case Token::kComma:      return os << "kComma";
    case Token::kEquality:   return os << "kEquality";
    case Token::kInequality: return os << "kInequality";
    case Token::kNot:        return os << "kNot";
    case Token::kOr:         return os << "kOr";
    case Token::kAnd:        return os << "kAnd";
    case Token::kForall:     return os << "kForall";
    case Token::kExists:     return os << "kExists";
    case Token::kRArrow:     return os << "kRArrow";
    case Token::kLRArrow:    return os << "kLRArrow";
    case Token::kAssign:     return os << "kAssign";
    case Token::kSlash:      return os << "kSlash";
    case Token::kComment:    return os << "kComment";
    case Token::kLeftParen:  return os << "kLeftParen";
    case Token::kRightParen: return os << "kRightParen";
    case Token::kUint:       return os << "kUint";
    case Token::kIdentifier: return os << "kIdentifier";
    case Token::kError:      return os << "kError";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Token& t) {
  return os << "Token(" << t.id() << "," << t.str() << ")";
}

}  // namespace pdl
}  // namespace format
}  // namespace lela

#endif  // LELA_FORMAT_PDL_LEXER_H_

