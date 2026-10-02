#include "core/rope.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace qce {

namespace detail {

struct Child {
  QExplicitlySharedDataPointer<Node> node;
  TextSummary sum;
};

// A leaf (height 0) holds text; a branch holds 1..MaxChildren children one level down.
struct Node : QSharedData {
  QString text;
  std::vector<Child> kids;
  TextSummary total;
  int height = 0;
  bool isLeaf() const { return height == 0; }
};

} // namespace detail

namespace {

using detail::Child;
using detail::Node;
using NodePtr = QExplicitlySharedDataPointer<Node>;

constexpr qsizetype kMaxLeaf = Rope::MaxLeafSize;
constexpr qsizetype kMinLeaf = Rope::MinLeafSize;
constexpr qsizetype kBuildLeaf = 1024; // bulk-built leaves leave headroom for typing
constexpr size_t kMaxKids = Rope::MaxChildren;

struct Pair {
  NodePtr l, r;
};

qsizetype countNewlines(QStringView text) {
  const char16_t *p = text.utf16();
  return std::count(p, p + text.size(), u'\n');
}

NodePtr makeLeaf(QString text) {
  auto *n = new Node;
  n->total = {text.size(), countNewlines(text)};
  n->text = std::move(text);
  return NodePtr(n);
}

Child childOf(NodePtr node) {
  const TextSummary sum = node->total;
  return Child{std::move(node), sum};
}

NodePtr makeBranch(std::vector<Child> kids) {
  auto *n = new Node;
  n->height = kids.front().node->height + 1;
  for (const Child &c : kids) {
    n->total.length += c.sum.length;
    n->total.newlines += c.sum.newlines;
  }
  n->kids = std::move(kids);
  return NodePtr(n);
}

// Branch over kids[from, to), or the lone child, or null.
NodePtr branchOf(const std::vector<Child> &kids, size_t from, size_t to) {
  if (from >= to)
    return {};
  if (to - from == 1)
    return kids[from].node;
  return makeBranch(std::vector<Child>(kids.begin() + qptrdiff(from), kids.begin() + qptrdiff(to)));
}

Pair packKids(std::vector<Child> kids) {
  if (kids.size() <= kMaxKids)
    return {makeBranch(std::move(kids)), {}};
  const size_t half = kids.size() / 2;
  std::vector<Child> second(kids.begin() + qptrdiff(half), kids.end());
  kids.resize(half);
  return {makeBranch(std::move(kids)), makeBranch(std::move(second))};
}

// Joins two leaves: merged if they fit, rebalanced if one is underfull, otherwise kept.
Pair joinLeaves(const NodePtr &a, const NodePtr &b) {
  const qsizetype la = a->text.size(), lb = b->text.size();
  if (la + lb <= kMaxLeaf)
    return {makeLeaf(a->text + b->text), {}};
  if (la < kMinLeaf || lb < kMinLeaf) {
    const QString all = a->text + b->text;
    const qsizetype half = all.size() / 2;
    return {makeLeaf(all.left(half)), makeLeaf(all.mid(half))};
  }
  return {a, b};
}

// Repairs underfull leaves among kids (all leaves) around indexes [first, last].
void normalizeLeaves(std::vector<Child> &kids, size_t first, size_t last) {
  size_t i = first > 0 ? first - 1 : 0;
  while (i + 1 < kids.size() && i <= last + 1) {
    const NodePtr &a = kids[i].node;
    const NodePtr &b = kids[i + 1].node;
    if (a->text.size() >= kMinLeaf && b->text.size() >= kMinLeaf) {
      ++i;
      continue;
    }
    Pair p = joinLeaves(a, b);
    kids[i] = childOf(p.l);
    if (p.r) {
      kids[i + 1] = childOf(p.r);
      ++i;
    } else {
      kids.erase(kids.begin() + qptrdiff(i) + 1);
    }
  }
}

// Joins two trees of any heights; both results have height max(ha, hb) (r is null if one node suffices).
Pair joinRec(const NodePtr &a, const NodePtr &b) {
  const int ha = a->height, hb = b->height;
  if (ha == hb) {
    if (ha == 0)
      return joinLeaves(a, b);
    Pair seam = joinRec(a->kids.back().node, b->kids.front().node);
    std::vector<Child> kids(a->kids.begin(), a->kids.end() - 1);
    const size_t seamIdx = kids.size();
    kids.push_back(childOf(seam.l));
    if (seam.r)
      kids.push_back(childOf(seam.r));
    kids.insert(kids.end(), b->kids.begin() + 1, b->kids.end());
    if (ha == 1)
      normalizeLeaves(kids, seamIdx, seamIdx + (seam.r ? 1 : 0));
    return packKids(std::move(kids));
  }
  if (ha > hb) {
    Pair p = joinRec(a->kids.back().node, b);
    std::vector<Child> kids(a->kids.begin(), a->kids.end() - 1);
    const size_t seamIdx = kids.size();
    kids.push_back(childOf(p.l));
    if (p.r)
      kids.push_back(childOf(p.r));
    if (ha == 1)
      normalizeLeaves(kids, seamIdx, seamIdx + (p.r ? 1 : 0));
    return packKids(std::move(kids));
  }
  Pair p = joinRec(a, b->kids.front().node);
  std::vector<Child> kids;
  kids.push_back(childOf(p.l));
  if (p.r)
    kids.push_back(childOf(p.r));
  const size_t seamEnd = kids.size() - 1;
  kids.insert(kids.end(), b->kids.begin() + 1, b->kids.end());
  if (hb == 1)
    normalizeLeaves(kids, 0, seamEnd);
  return packKids(std::move(kids));
}

NodePtr join(const NodePtr &a, const NodePtr &b) {
  if (!a)
    return b;
  if (!b)
    return a;
  Pair p = joinRec(a, b);
  if (p.r)
    return makeBranch({childOf(p.l), childOf(p.r)});
  return p.l;
}

// Drops single-child chains at the top.
NodePtr trim(NodePtr root) {
  while (root && !root->isLeaf() && root->kids.size() == 1) {
    NodePtr child = root->kids.front().node;
    root = std::move(child);
  }
  return root;
}

// Splits at `at`: l holds [0, at), r holds [at, length).
Pair splitAt(const NodePtr &node, qsizetype at) {
  if (at <= 0)
    return {{}, node};
  if (at >= node->total.length)
    return {node, {}};
  if (node->isLeaf())
    return {makeLeaf(node->text.left(at)), makeLeaf(node->text.mid(at))};

  const std::vector<Child> &kids = node->kids;
  size_t i = 0;
  qsizetype pos = 0;
  while (i + 1 < kids.size() && at >= pos + kids[i].sum.length) {
    pos += kids[i].sum.length;
    ++i;
  }
  if (at == pos)
    return {branchOf(kids, 0, i), branchOf(kids, i, kids.size())};
  Pair p = splitAt(kids[i].node, at - pos);
  return {join(branchOf(kids, 0, i), p.l), join(p.r, branchOf(kids, i + 1, kids.size()))};
}

// Fast path: insert into a single leaf that has room. Returns null if the leaf would overflow.
NodePtr insertInLeaf(const NodePtr &node, qsizetype at, QStringView text, TextSummary add) {
  if (node->isLeaf()) {
    if (node->text.size() + text.size() > kMaxLeaf)
      return {};
    auto *n = new Node;
    n->text = node->text;
    n->text.insert(at, text);
    n->total = {node->total.length + add.length, node->total.newlines + add.newlines};
    return NodePtr(n);
  }
  size_t i = 0;
  qsizetype pos = 0;
  while (i + 1 < node->kids.size() && at > pos + node->kids[i].sum.length) {
    pos += node->kids[i].sum.length;
    ++i;
  }
  NodePtr child = insertInLeaf(node->kids[i].node, at - pos, text, add);
  if (!child)
    return {};
  auto *n = new Node(*node);
  n->kids[i] = childOf(std::move(child));
  n->total.length += add.length;
  n->total.newlines += add.newlines;
  return NodePtr(n);
}

// Fast path: remove [s, e) lying inside one leaf, as long as the leaf stays at least minimally full.
NodePtr removeInLeaf(const NodePtr &node, qsizetype s, qsizetype e, bool isRoot, TextSummary *removed) {
  if (node->isLeaf()) {
    const qsizetype remaining = node->text.size() - (e - s);
    if (remaining < kMinLeaf && !(isRoot && remaining > 0))
      return {};
    const QStringView gone = QStringView(node->text).mid(s, e - s);
    *removed = {e - s, countNewlines(gone)};
    auto *n = new Node;
    n->text = node->text;
    n->text.remove(s, e - s);
    n->total = {node->total.length - removed->length, node->total.newlines - removed->newlines};
    return NodePtr(n);
  }
  size_t i = 0;
  qsizetype pos = 0;
  while (i + 1 < node->kids.size() && s >= pos + node->kids[i].sum.length) {
    pos += node->kids[i].sum.length;
    ++i;
  }
  if (e > pos + node->kids[i].sum.length)
    return {};
  NodePtr child = removeInLeaf(node->kids[i].node, s - pos, e - pos, false, removed);
  if (!child)
    return {};
  auto *n = new Node(*node);
  n->kids[i] = childOf(std::move(child));
  n->total.length -= removed->length;
  n->total.newlines -= removed->newlines;
  return NodePtr(n);
}

} // namespace

// ---- Rope ----

Rope::Rope() = default;
Rope::Rope(const Rope &other) = default;
Rope::Rope(Rope &&other) noexcept = default;
Rope &Rope::operator=(const Rope &other) = default;
Rope &Rope::operator=(Rope &&other) noexcept = default;
Rope::~Rope() = default;
Rope::Rope(NodePtr root) : m_root(trim(std::move(root))) {}

Rope Rope::fromString(QStringView text) {
  RopeBuilder b;
  b.append(text);
  return b.finish();
}

qsizetype Rope::length() const { return m_root ? m_root->total.length : 0; }
qsizetype Rope::newlineCount() const { return m_root ? m_root->total.newlines : 0; }
TextSummary Rope::summary() const { return m_root ? m_root->total : TextSummary{}; }

QChar Rope::at(qsizetype offset) const {
  Q_ASSERT(offset >= 0 && offset < length());
  const Node *n = m_root.data();
  while (!n->isLeaf()) {
    for (const Child &c : n->kids) {
      if (offset < c.sum.length) {
        n = c.node.data();
        break;
      }
      offset -= c.sum.length;
    }
  }
  return n->text.at(offset);
}

Rope Rope::slice(qsizetype start, qsizetype end) const {
  start = qBound<qsizetype>(0, start, length());
  end = qBound<qsizetype>(start, end, length());
  if (start == end)
    return {};
  if (start == 0 && end == length())
    return *this;
  Pair left = splitAt(m_root, end);
  return Rope(splitAt(left.l, start).r);
}

QString Rope::toString() const { return toString(0, length()); }

QString Rope::toString(qsizetype start, qsizetype end) const {
  start = qBound<qsizetype>(0, start, length());
  end = qBound<qsizetype>(start, end, length());
  QString out;
  out.reserve(end - start);
  ChunkIterator it(*this, start);
  QStringView chunk;
  qsizetype pos = start;
  while (pos < end && it.next(&chunk)) {
    if (pos + chunk.size() > end)
      chunk = chunk.first(end - pos);
    out.append(chunk);
    pos += chunk.size();
  }
  return out;
}

Rope Rope::insert(qsizetype offset, QStringView text) const {
  if (text.isEmpty())
    return *this;
  if (!m_root)
    return fromString(text);
  offset = qBound<qsizetype>(0, offset, length());
  if (text.size() <= kMaxLeaf) {
    if (NodePtr n = insertInLeaf(m_root, offset, text, {text.size(), countNewlines(text)}))
      return Rope(n);
  }
  Pair p = splitAt(m_root, offset);
  return Rope(join(join(p.l, fromString(text).m_root), p.r));
}

Rope Rope::insert(qsizetype offset, const Rope &text) const {
  if (text.isEmpty())
    return *this;
  offset = qBound<qsizetype>(0, offset, length());
  Pair p = splitAt(m_root, offset);
  return Rope(join(join(p.l, text.m_root), p.r));
}

Rope Rope::remove(qsizetype start, qsizetype end) const {
  start = qBound<qsizetype>(0, start, length());
  end = qBound<qsizetype>(start, end, length());
  if (start == end)
    return *this;
  if (start == 0 && end == length())
    return {};
  TextSummary removed;
  if (NodePtr n = removeInLeaf(m_root, start, end, true, &removed))
    return Rope(n);
  return Rope(join(splitAt(m_root, start).l, splitAt(m_root, end).r));
}

Rope Rope::replace(qsizetype start, qsizetype end, QStringView text) const {
  start = qBound<qsizetype>(0, start, length());
  end = qBound<qsizetype>(start, end, length());
  return remove(start, end).insert(start, text);
}

Rope Rope::concat(const Rope &other) const { return Rope(join(m_root, other.m_root)); }

bool Rope::validate(QString *problem) const {
  auto fail = [&](const QString &why) {
    if (problem)
      *problem = why;
    return false;
  };
  if (!m_root)
    return true;
  struct Rec {
    static bool run(const Node *n, bool isRoot, QString *why) {
      if (n->isLeaf()) {
        if (n->text.isEmpty()) {
          *why = QStringLiteral("empty leaf");
          return false;
        }
        if (n->text.size() > kMaxLeaf) {
          *why = QStringLiteral("oversized leaf");
          return false;
        }
        if (n->total.length != n->text.size() || n->total.newlines != countNewlines(n->text)) {
          *why = QStringLiteral("leaf summary mismatch");
          return false;
        }
        return true;
      }
      if (n->kids.empty() || n->kids.size() > kMaxKids) {
        *why = QStringLiteral("bad fan-out");
        return false;
      }
      Q_UNUSED(isRoot)
      TextSummary sum;
      for (const Child &c : n->kids) {
        if (c.node->height != n->height - 1) {
          *why = QStringLiteral("uneven depth");
          return false;
        }
        if (c.sum.length != c.node->total.length || c.sum.newlines != c.node->total.newlines) {
          *why = QStringLiteral("child summary mismatch");
          return false;
        }
        sum.length += c.sum.length;
        sum.newlines += c.sum.newlines;
        if (!run(c.node.data(), false, why))
          return false;
      }
      if (sum.length != n->total.length || sum.newlines != n->total.newlines) {
        *why = QStringLiteral("branch summary mismatch");
        return false;
      }
      return true;
    }
  };
  QString why;
  if (!Rec::run(m_root.data(), true, &why))
    return fail(why);
  return true;
}

Rope::Stats Rope::stats() const {
  Stats s;
  if (!m_root)
    return s;
  s.height = m_root->height;
  struct Rec {
    static void run(const Node *n, Stats *s) {
      if (n->isLeaf()) {
        ++s->leaves;
        if (n->text.size() < kMinLeaf)
          ++s->underfullLeaves;
        return;
      }
      ++s->branches;
      for (const Child &c : n->kids)
        run(c.node.data(), s);
    }
  };
  Rec::run(m_root.data(), &s);
  return s;
}

// ---- RopeBuilder ----

struct RopeBuilder::Impl {
  QString tail;
  std::vector<std::vector<NodePtr>> levels; // levels[k]: pending nodes of height k, < MaxChildren each

  void pushLeaf(QString text) {
    if (levels.empty())
      levels.emplace_back();
    push(0, makeLeaf(std::move(text)));
  }

  void push(size_t level, NodePtr node) {
    if (levels.size() <= level)
      levels.resize(level + 1);
    levels[level].push_back(std::move(node));
    if (levels[level].size() == kMaxKids) {
      std::vector<Child> kids;
      kids.reserve(kMaxKids);
      for (NodePtr &n : levels[level])
        kids.push_back(childOf(std::move(n)));
      levels[level].clear();
      push(level + 1, makeBranch(std::move(kids)));
    }
  }

  NodePtr finish() {
    if (!tail.isEmpty())
      pushLeaf(std::exchange(tail, QString()));
    for (size_t k = 0; k < levels.size(); ++k) {
      if (levels[k].empty())
        continue;
      bool top = true;
      for (size_t j = k + 1; j < levels.size(); ++j)
        top = top && levels[j].empty();
      if (top && levels[k].size() == 1)
        return levels[k][0];
      std::vector<Child> kids;
      for (NodePtr &n : levels[k])
        kids.push_back(childOf(std::move(n)));
      levels[k].clear();
      push(k + 1, makeBranch(std::move(kids)));
    }
    return {};
  }
};

RopeBuilder::RopeBuilder() : d(new Impl) {}
RopeBuilder::~RopeBuilder() { delete d; }

void RopeBuilder::append(QStringView text) {
  m_length += text.size();
  while (!text.isEmpty()) {
    if (d->tail.isEmpty() && text.size() >= kBuildLeaf) {
      d->pushLeaf(text.first(kBuildLeaf).toString());
      text = text.sliced(kBuildLeaf);
      continue;
    }
    const qsizetype take = qMin(text.size(), kBuildLeaf - d->tail.size());
    d->tail.append(text.first(take));
    text = text.sliced(take);
    if (d->tail.size() == kBuildLeaf)
      d->pushLeaf(std::exchange(d->tail, QString()));
  }
}

Rope RopeBuilder::snapshot() const {
  Impl copy = *d;
  return Rope(copy.finish());
}

Rope RopeBuilder::finish() {
  Rope r(d->finish());
  d->levels.clear();
  m_length = 0;
  return r;
}

// ---- ChunkIterator ----

ChunkIterator::ChunkIterator(const Rope &rope, qsizetype from) : m_rope(rope) {
  const Node *n = m_rope.m_root.data();
  if (!n)
    return;
  from = qBound<qsizetype>(0, from, n->total.length);
  while (!n->isLeaf()) {
    size_t i = 0;
    while (i + 1 < n->kids.size() && from >= n->kids[i].sum.length) {
      from -= n->kids[i].sum.length;
      ++i;
    }
    m_stack.push_back({n, int(i) + 1});
    n = n->kids[i].node.data();
  }
  m_current = QStringView(n->text).sliced(from);
}

ChunkIterator::~ChunkIterator() = default;

bool ChunkIterator::nextLeaf(QStringView *leaf) {
  while (!m_stack.isEmpty()) {
    Frame &f = m_stack.last();
    if (size_t(f.index) >= f.node->kids.size()) {
      m_stack.removeLast();
      continue;
    }
    const Node *child = f.node->kids[size_t(f.index++)].node.data();
    if (child->isLeaf()) {
      *leaf = QStringView(child->text);
      return true;
    }
    m_stack.push_back({child, 0});
  }
  return false;
}

bool ChunkIterator::peek() {
  if (!m_hasPeeked) {
    m_hasPeeked = true;
    m_peekValid = nextLeaf(&m_peeked);
  }
  return m_peekValid;
}

bool ChunkIterator::advance(QStringView *leaf) {
  if (m_hasPeeked) {
    m_hasPeeked = false;
    if (!m_peekValid)
      return false;
    *leaf = m_peeked;
    return true;
  }
  return nextLeaf(leaf);
}

bool ChunkIterator::next(QStringView *chunk) {
  while (m_current.isEmpty()) {
    if (!advance(&m_current))
      return false;
  }
  if (m_current.back().isHighSurrogate() && peek() && m_peeked.front().isLowSurrogate()) {
    if (m_current.size() > 1) {
      *chunk = m_current.chopped(1);
      m_current = m_current.last(1);
      return true;
    }
    m_scratch[0] = m_current.front();
    m_scratch[1] = m_peeked.front();
    m_current = m_peeked.sliced(1);
    m_hasPeeked = false;
    *chunk = QStringView(m_scratch, 2);
    return true;
  }
  *chunk = m_current;
  m_current = {};
  return true;
}

} // namespace qce
