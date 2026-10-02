#include "linelayoutcache.h"

namespace qce {

void LineLayoutCache::setCapacity(qsizetype capacity) {
  m_capacity = qMax<qsizetype>(1, capacity);
  evictToCapacity();
}

std::shared_ptr<LineLayout> LineLayoutCache::find(qsizetype line) {
  const auto it = m_index.find(line);
  if (it == m_index.end())
    return {};
  m_entries.splice(m_entries.begin(), m_entries, it->second);
  ++m_stats.hits;
  return it->second->value;
}

std::shared_ptr<LineLayout>
LineLayoutCache::insert(qsizetype line, std::unique_ptr<QTextLayout> layout, qreal width, QString text) {
  if (const auto it = m_index.find(line); it != m_index.end()) {
    m_entries.erase(it->second);
    m_index.erase(it);
  }
  auto value = std::make_shared<LineLayout>();
  value->id = m_nextId++;
  value->layout = std::move(layout);
  value->width = width;
  value->text = std::move(text);
  m_entries.push_front({line, value});
  m_index[line] = m_entries.begin();
  ++m_stats.created;
  evictToCapacity();
  return value;
}

void LineLayoutCache::evictToCapacity() {
  while (qsizetype(m_entries.size()) > m_capacity) {
    m_index.erase(m_entries.back().line);
    m_entries.pop_back();
    ++m_stats.evicted;
  }
}

void LineLayoutCache::invalidate(qsizetype firstLine, qsizetype oldCount, qsizetype newCount) {
  const qsizetype oldEnd = firstLine + oldCount;
  const qsizetype delta = newCount - oldCount;
  m_index.clear();
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->line >= firstLine && it->line < oldEnd) {
      it = m_entries.erase(it);
      continue;
    }
    if (it->line >= oldEnd)
      it->line += delta;
    m_index[it->line] = it;
    ++it;
  }
}

void LineLayoutCache::clear(qsizetype firstLine) {
  if (firstLine <= 0) {
    m_entries.clear();
    m_index.clear();
    return;
  }
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->line >= firstLine) {
      m_index.erase(it->line);
      it = m_entries.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace qce
