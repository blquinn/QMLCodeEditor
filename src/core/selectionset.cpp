#include "core/selectionset.h"

#include <algorithm>
#include <numeric>

namespace qce {

SelectionSet::SelectionSet(TextDocument *document, QObject *parent)
    : QObject(parent), m_document(document) {
  set({{0, 0}});
  connect(document, &TextDocument::changed, this, &SelectionSet::onDocumentChanged);
  connect(document, &TextDocument::textReset, this, &SelectionSet::onDocumentReset);
}

SelectionSet::~SelectionSet() { removeAnchors(); }

void SelectionSet::removeAnchors() {
  AnchorSet &anchors = m_document->anchors();
  for (const Entry &e : m_entries) {
    anchors.remove(e.anchor);
    anchors.remove(e.head);
  }
  m_entries.clear();
}

Selection SelectionSet::at(int index) const {
  const Entry &e = m_entries[size_t(index)];
  const AnchorSet &anchors = m_document->anchors();
  return {anchors.offset(e.anchor), anchors.offset(e.head)};
}

SelectionList SelectionSet::selections() const {
  SelectionList list;
  list.reserve(count());
  for (int i = 0; i < count(); ++i)
    list.append(at(i));
  return list;
}

namespace {

// Sorts by start and merges overlapping or touching selections; `primary` becomes the index of
// whatever the primary was merged into.
SelectionList mergeSelections(const SelectionList &list, int &primary) {
  std::vector<int> order(size_t(list.size()));
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return list[a].start() != list[b].start() ? list[a].start() < list[b].start()
                                              : list[a].end() < list[b].end();
  });
  SelectionList merged;
  merged.reserve(list.size());
  int newPrimary = 0;
  for (int index : order) {
    const Selection s = list[index];
    if (!merged.isEmpty() && s.start() <= merged.last().end()) {
      Selection &last = merged.last();
      // Keep the direction of whichever selection is larger.
      const bool backward = s.end() - s.start() > last.end() - last.start() ? s.head < s.anchor
                                                                           : last.head < last.anchor;
      const qsizetype start = qMin(last.start(), s.start()), end = qMax(last.end(), s.end());
      last = backward ? Selection{end, start} : Selection{start, end};
    } else {
      merged.append(s);
    }
    if (index == primary)
      newPrimary = int(merged.size()) - 1;
  }
  primary = newPrimary;
  return merged;
}

bool isOrdered(const SelectionList &list) {
  for (qsizetype i = 1; i < list.size(); ++i)
    if (list[i].start() <= list[i - 1].end())
      return false;
  return true;
}

} // namespace

// Puts the entries on `list` (sorted and disjoint). Existing anchors are moved rather than
// recreated, so replacing a big set with a similar one costs no allocation and, when nothing moved,
// no anchor work at all.
void SelectionSet::assign(const SelectionList &list, int primary) {
  AnchorSet &anchors = m_document->anchors();
  const size_t n = size_t(list.size());
  while (m_entries.size() > n) {
    anchors.remove(m_entries.back().anchor);
    anchors.remove(m_entries.back().head);
    m_entries.pop_back();
  }
  m_entries.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const Selection &s = list[qsizetype(i)];
    if (i < m_entries.size()) {
      Entry &e = m_entries[i];
      e.goalX = NoGoal;
      if (anchors.offset(e.anchor) != s.anchor)
        anchors.move(e.anchor, s.anchor);
      if (anchors.offset(e.head) != s.head)
        anchors.move(e.head, s.head);
    } else {
      Entry e;
      e.anchor = anchors.create(s.anchor, Gravity::Right);
      e.head = anchors.create(s.head, Gravity::Right);
      m_entries.push_back(e);
    }
  }
  m_primary = primary;
}

void SelectionSet::set(const SelectionList &selections, int primary) {
  const Rope &rope = m_document->rope();
  const qsizetype length = rope.length();
  auto clean = [&](qsizetype offset) { return rope.snapToCodePoint(qBound<qsizetype>(0, offset, length)); };

  SelectionList list = selections.isEmpty() ? SelectionList{{0, 0}} : selections;
  primary = qBound(0, primary, int(list.size()) - 1);
  for (Selection &s : list) {
    s.anchor = clean(s.anchor);
    s.head = clean(s.head);
  }
  if (!isOrdered(list))
    list = mergeSelections(list, primary);
  assign(list, primary);
  m_needsNormalize = false;
  m_dirty = true;
  if (m_batch == 0)
    notify();
}

void SelectionSet::setPrimary(int index) {
  index = qBound(0, index, count() - 1);
  if (index == m_primary)
    return;
  m_primary = index;
  m_dirty = true;
  if (m_batch == 0)
    notify();
}

int SelectionSet::lowerBound(qsizetype offset) const {
  const AnchorSet &anchors = m_document->anchors();
  int lo = 0, hi = count();
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    const Entry &e = m_entries[size_t(mid)];
    if (qMax(anchors.offset(e.anchor), anchors.offset(e.head)) < offset)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

int SelectionSet::indexAt(qsizetype offset) const {
  const int i = lowerBound(offset);
  return i < count() && at(i).start() <= offset ? i : -1;
}

void SelectionSet::add(Selection selection) {
  const Rope &rope = m_document->rope();
  const qsizetype length = rope.length();
  auto clean = [&](qsizetype offset) { return rope.snapToCodePoint(qBound<qsizetype>(0, offset, length)); };
  selection = {clean(selection.anchor), clean(selection.head)};
  // Where it goes, when it touches nothing: a plain insertion that leaves every other anchor alone.
  const int at = lowerBound(selection.start());
  const bool touchesNext = at < count() && this->at(at).start() <= selection.end();
  if (touchesNext) {
    SelectionList list = selections();
    list.append(selection);
    set(list, int(list.size()) - 1);
    return;
  }
  AnchorSet &anchors = m_document->anchors();
  Entry e;
  e.anchor = anchors.create(selection.anchor, Gravity::Right);
  e.head = anchors.create(selection.head, Gravity::Right);
  m_entries.insert(m_entries.begin() + at, e);
  m_primary = at;
  m_dirty = true;
  if (m_batch == 0)
    notify();
}

void SelectionSet::collapseToPrimary() {
  if (count() > 1)
    set({primary()});
}

void SelectionSet::normalize() {
  m_needsNormalize = false;
  qsizetype previousEnd = -1;
  bool overlap = false;
  for (int i = 0; i < count() && !overlap; ++i) {
    const Selection s = at(i);
    overlap = i > 0 && s.start() <= previousEnd;
    previousEnd = s.end();
  }
  if (!overlap)
    return;
  int primary = m_primary;
  assign(mergeSelections(selections(), primary), primary);
}

void SelectionSet::notify() {
  m_dirty = false;
  if (m_needsNormalize)
    normalize();
  emit changed();
}

void SelectionSet::onDocumentChanged(const TextChange &change) {
  if (m_document->isLoading()) {
    // A load appends to an empty document; the cursor stays at the start rather than riding along.
    set({{0, 0}});
    return;
  }
  // Removing text can pull selections together; they are merged when the edit (or batch) is over.
  if (change.oldEnd > change.start)
    m_needsNormalize = true;
  m_dirty = true;
  if (m_batch == 0)
    notify();
}

void SelectionSet::onDocumentReset() { set({{0, 0}}); }

} // namespace qce
