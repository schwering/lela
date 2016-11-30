// vim:filetype=cpp:textwidth=120:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014--2016 Christoph Schwering

#ifndef EXAMPLES_MINESWEEPER_AGENT_H_
#define EXAMPLES_MINESWEEPER_AGENT_H_

#include <iostream>

#include "game.h"
#include "kb.h"

class Agent {
 public:
  virtual ~Agent() {}
  virtual void Explore() = 0;
};

class HumanAgent : public Agent {
 public:
  explicit HumanAgent(Game* g, std::ostream* os, std::istream* is) : g_(g), os_(os), is_(is) {}

  void Explore() override {
    for (;;) {
      Point p;
      *os_ << "Exploring X and Y coordinates: ";
      *is_ >> p.x >> p.y;
      if (!g_->valid(p) || g_->opened(p)) {
        std::cout << "Invalid coordinates, repeat" << std::endl;
        continue;
      }
      g_->OpenWithFrontier(p);
      return;
    }
  }

 private:
  Game* g_;
  std::ostream* os_;
  std::istream* is_;
};

class KnowledgeBaseAgent : public Agent {
 public:
  explicit KnowledgeBaseAgent(Game* g, KnowledgeBase* kb, std::ostream* os) : g_(g), kb_(kb), os_(os) {}

  void Explore() override {
    kb_->Sync();

    // First open a random point which is not at the edge of the field.
    if (g_->n_opens() == 0) {
      Point p;
      do {
        p = g_->RandomPoint();
      } while (g_->neighbors_of(p).size() < 8);
      *os_ << "Exploring X and Y coordinates: " << p.x << " " << p.y << " chosen at random" << std::endl;
      g_->OpenWithFrontier(p);
      return;
    }

    // First look for a field which is known not to be a mine.
    for (int k = 0; k <= kb_->max_k(); ++k) {
      for (size_t index = 0; index < g_->n_fields(); ++index) {
        const Point p = g_->to_point(index);
        if (g_->opened(p) || g_->flagged(p)) {
          continue;
        }
        const lela::internal::Maybe<bool> r = kb_->IsMine(p, k);
        if (r.yes) {
          if (r.val) {
            *os_ << "Flagging X and Y coordinates: " << p.x << " " << p.y << " found at split level " << k << std::endl;
            g_->Flag(p);
          } else {
            *os_ << "Exploring X and Y coordinates: " << p.x << " " << p.y << " found at split level " << k << std::endl;
            g_->OpenWithFrontier(p);
          }
          return;
        }
      }
    }

    // Didn't find any reliable action, so we need to guess.
    for (size_t index = 0; index < g_->n_fields(); ++index) {
      const Point p = g_->to_point(index);
      if (g_->opened(p) || g_->flagged(p)) {
        continue;
      }
      *os_ << "Exploring X and Y coordinates: " << p.x << " " << p.y << ", which is just a guess." << std::endl;
      g_->OpenWithFrontier(p);
      return;
    }

    // That's weird, this case should never occur, because our reasoning should
    // be correct.
    std::cerr << __FILE__ << ":" << __LINE__ << ": Why didn't we choose a point?" << std::endl;
    assert(false);
  }

 private:
  Game* g_;
  KnowledgeBase* kb_;
  std::ostream* os_;
};

#endif  // EXAMPLES_MINESWEEPER_AGENT_H_
