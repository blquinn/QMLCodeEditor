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

  // Sort by start, remembering where the primary went, then merge overlapping or touching ones.
  std::vector<int> order(size_t(list.size()));
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return list[a].start() != list[b].start() ? list[a].start() < list[b].start()
                                              : list[a].end() < list[b].end();
  });
  SelectionList merged;
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

  removeAnchors();
  AnchorSet &anchors = m_document->anchors();
  m_entries.reserve(size_t(merged.size()));
  for (const Selection &s : std::as_const(merged)) {
    Entry e;
    e.anchor = anchors.create(s.anchor, Gravity::Right);
    e.head = anchors.create(s.head, Gravity::Right);
    m_entries.push_back(e);
  }
  m_primary = newPrimary;
  m_dirty = true;
  if (m_batch == 0)
    notify();
}

void SelectionSet::notify() {
  m_dirty = false;
  emit changed();
}

void SelectionSet::onDocumentChanged() {
  if (m_document->isLoading()) {
    // A load appends to an empty document; the cursor stays at the start rather than riding along.
    set({{0, 0}});
    return;
  }
  m_dirty = true;
  if (m_batch == 0)
    notify();
}

void SelectionSet::onDocumentReset() { set({{0, 0}}); }

} // namespace qce
