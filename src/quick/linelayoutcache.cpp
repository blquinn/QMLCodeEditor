#include "linelayoutcache.h"

namespace qce {

void LineLayoutCache::setCapacity(qsizetype capacity) {
  m_capacity = qMax<qsizetype>(1, capacity);
  evictToCapacity();
}

void LineLayoutCache::setByteCapacity(qsizetype bytes) {
  m_byteCapacity = qMax<qsizetype>(1, bytes);
  evictToCapacity();
}

void LineLayoutCache::chargeNewest(qsizetype bytes) {
  if (m_entries.empty())
    return;
  m_bytes += bytes - m_entries.front().bytes;
  m_entries.front().bytes = bytes;
  evictToCapacity();
}

std::shared_ptr<LineLayout> LineLayoutCache::find(qsizetype line, qsizetype rowInLine) {
  const auto it = m_index.find({line, rowInLine});
  if (it == m_index.end())
    return {};
  m_entries.splice(m_entries.begin(), m_entries, it->second);
  ++m_stats.hits;
  return it->second->value;
}

std::shared_ptr<LineLayout>
LineLayoutCache::insert(
  qsizetype line, std::unique_ptr<QTextLayout> layout, qreal width, QString text, qsizetype rowInLine
) {
  const Key key{line, rowInLine};
  if (const auto it = m_index.find(key); it != m_index.end()) {
    m_bytes -= it->second->bytes;
    m_entries.erase(it->second);
    m_index.erase(it);
  }
  auto value = std::make_shared<LineLayout>();
  value->id = m_nextId++;
  value->layout = std::move(layout);
  value->width = width;
  value->fullWidth = width;
  value->text = std::move(text);
  m_entries.push_front({key, value});
  m_index[key] = m_entries.begin();
  ++m_stats.created;
  evictToCapacity();
  return value;
}

void LineLayoutCache::evictToCapacity() {
  while (qsizetype(m_entries.size()) > m_capacity || (m_bytes > m_byteCapacity && m_entries.size() > 1)) {
    m_index.erase(m_entries.back().key);
    m_bytes -= m_entries.back().bytes;
    m_entries.pop_back();
    ++m_stats.evicted;
  }
}

void LineLayoutCache::invalidate(qsizetype firstLine, qsizetype oldCount, qsizetype newCount) {
  const qsizetype oldEnd = firstLine + oldCount;
  const qsizetype delta = newCount - oldCount;
  m_index.clear();
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->key.first >= firstLine && it->key.first < oldEnd) {
      m_bytes -= it->bytes;
      it = m_entries.erase(it);
      continue;
    }
    if (it->key.first >= oldEnd)
      it->key.first += delta;
    m_index[it->key] = it;
    ++it;
  }
}

void LineLayoutCache::clear(qsizetype firstLine) {
  if (firstLine <= 0) {
    m_entries.clear();
    m_index.clear();
    m_bytes = 0;
    return;
  }
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->key.first >= firstLine) {
      m_index.erase(it->key);
      m_bytes -= it->bytes;
      it = m_entries.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace qce
