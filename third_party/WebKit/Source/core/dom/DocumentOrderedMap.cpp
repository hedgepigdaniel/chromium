/*
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/dom/DocumentOrderedMap.h"

#include "core/HTMLNames.h"
#include "core/dom/Element.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/TreeScope.h"
#include "core/html/HTMLMapElement.h"
#include "core/html/HTMLSlotElement.h"

namespace blink {

using namespace HTMLNames;

DocumentOrderedMap* DocumentOrderedMap::Create() {
  return new DocumentOrderedMap;
}

DocumentOrderedMap::DocumentOrderedMap() {}

#if DCHECK_IS_ON()
static int g_remove_scope_level = 0;

DocumentOrderedMap::RemoveScope::RemoveScope() {
  g_remove_scope_level++;
}

DocumentOrderedMap::RemoveScope::~RemoveScope() {
  DCHECK(g_remove_scope_level);
  g_remove_scope_level--;
}
#endif

inline bool KeyMatchesId(const AtomicString& key, const Element& element) {
  return element.GetIdAttribute() == key;
}

inline bool KeyMatchesMapName(const AtomicString& key, const Element& element) {
  return isHTMLMapElement(element) &&
         toHTMLMapElement(element).GetName() == key;
}

inline bool KeyMatchesSlotName(const AtomicString& key,
                               const Element& element) {
  return isHTMLSlotElement(element) &&
         toHTMLSlotElement(element).GetName() == key;
}

void DocumentOrderedMap::Add(const AtomicString& key, Element* element) {
  DCHECK(key);
  DCHECK(element);

  Map::AddResult add_result = map_.insert(key, new MapEntry(element));
  if (add_result.is_new_entry)
    return;

  Member<MapEntry>& entry = add_result.stored_value->value;
  DCHECK(entry->count);
  entry->element = nullptr;
  entry->count++;
  entry->ordered_list.clear();
}

void DocumentOrderedMap::Remove(const AtomicString& key, Element* element) {
  DCHECK(key);
  DCHECK(element);

  Map::iterator it = map_.find(key);
  if (it == map_.end())
    return;

  Member<MapEntry>& entry = it->value;
  DCHECK(entry->count);
  if (entry->count == 1) {
    DCHECK(!entry->element || entry->element == element);
    map_.erase(it);
  } else {
    if (entry->element == element) {
      DCHECK(entry->ordered_list.IsEmpty() ||
             entry->ordered_list.front() == element);
      entry->element =
          entry->ordered_list.size() > 1 ? entry->ordered_list[1] : nullptr;
    }
    entry->count--;
    entry->ordered_list.clear();
  }
}

template <bool keyMatches(const AtomicString&, const Element&)>
inline Element* DocumentOrderedMap::Get(const AtomicString& key,
                                        const TreeScope* scope) const {
  DCHECK(key);
  DCHECK(scope);

  MapEntry* entry = map_.at(key);
  if (!entry)
    return 0;

  DCHECK(entry->count);
  if (entry->element)
    return entry->element;

  // Iterate to find the node that matches. Nothing will match iff an element
  // with children having duplicate IDs is being removed -- the tree traversal
  // will be over an updated tree not having that subtree. In all other cases,
  // a match is expected.
  for (Element& element : ElementTraversal::StartsAfter(scope->RootNode())) {
    if (!keyMatches(key, element))
      continue;
    entry->element = &element;
    return &element;
  }
// As get()/getElementById() can legitimately be called while handling element
// removals, allow failure iff we're in the scope of node removals.
#if DCHECK_IS_ON()
  DCHECK(g_remove_scope_level);
#endif
  return 0;
}

Element* DocumentOrderedMap::GetElementById(const AtomicString& key,
                                            const TreeScope* scope) const {
  return Get<KeyMatchesId>(key, scope);
}

const HeapVector<Member<Element>>& DocumentOrderedMap::GetAllElementsById(
    const AtomicString& key,
    const TreeScope* scope) const {
  DCHECK(key);
  DCHECK(scope);
  DEFINE_STATIC_LOCAL(HeapVector<Member<Element>>, empty_vector,
                      (new HeapVector<Member<Element>>));

  Map::iterator it = map_.find(key);
  if (it == map_.end())
    return empty_vector;

  Member<MapEntry>& entry = it->value;
  DCHECK(entry->count);

  if (entry->ordered_list.IsEmpty()) {
    entry->ordered_list.ReserveCapacity(entry->count);
    for (Element* element =
             entry->element ? entry->element.Get()
                            : ElementTraversal::FirstWithin(scope->RootNode());
         entry->ordered_list.size() < entry->count;
         element = ElementTraversal::Next(*element)) {
      DCHECK(element);
      if (!KeyMatchesId(key, *element))
        continue;
      entry->ordered_list.UncheckedAppend(element);
    }
    if (!entry->element)
      entry->element = entry->ordered_list.front();
  }

  return entry->ordered_list;
}

Element* DocumentOrderedMap::GetElementByMapName(const AtomicString& key,
                                                 const TreeScope* scope) const {
  return Get<KeyMatchesMapName>(key, scope);
}

// TODO(hayato): Template get<> by return type.
HTMLSlotElement* DocumentOrderedMap::GetSlotByName(
    const AtomicString& key,
    const TreeScope* scope) const {
  if (Element* slot = Get<KeyMatchesSlotName>(key, scope)) {
    DCHECK(isHTMLSlotElement(slot));
    return toHTMLSlotElement(slot);
  }
  return nullptr;
}

DEFINE_TRACE(DocumentOrderedMap) {
  visitor->Trace(map_);
}

DEFINE_TRACE(DocumentOrderedMap::MapEntry) {
  visitor->Trace(element);
  visitor->Trace(ordered_list);
}

}  // namespace blink
