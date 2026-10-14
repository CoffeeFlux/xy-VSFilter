#pragma once

#include <stdint.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <afx.h>

#include "mru_cache.h"

template<>
struct std::equal_to<CRect>
{	// functor for operator==
    bool operator()(const CRect& _Left, const CRect& _Right) const
    {	// apply operator== to operands
        return (_Left == _Right)==TRUE;
    }
};

template<
    typename V,
    int DEFAULT_CACHE_SIZE = 1024,
    class VTraits=CElementTraits<V>
>
class XyFlyWeight
{
public:
    typedef std::size_t IdType;
    typedef std::shared_ptr<const V> SharedConstV;

    class SharedVElementTraits:public CElementTraits<SharedConstV>
    {
    public:
        static inline ULONG Hash(const SharedConstV& v)
        {
            return VTraits::Hash(*v);
        }
        static inline bool CompareElements(
            const SharedConstV& element1,
            const SharedConstV& element2)
        {
            return VTraits::CompareElements(*element1, *element2);
        }
        static inline int CompareElementsOrdered(
            const SharedConstV& element1,
            const SharedConstV& element2)
        {
            return VTraits::CompareElementsOrdered(*element1, *element2);
        }
    };

    typedef EnhancedXyMru<SharedConstV, IdType, SharedVElementTraits> Cacher;    
public:
    static const int INVALID_ID = 0;
public:
    // v will be assigned to a shared pointer
    XyFlyWeight(const V *v):_v(v)
    {
        Cacher * cacher = GetCacher();
        ASSERT( cacher );
        bool new_item_added = false;
        POSITION pos = cacher->AddHeadIfNotExists(_v, INVALID_ID, &new_item_added);
        if ( !new_item_added )
        {
            cacher->UpdateCache(pos);
            _v = cacher->GetKeyAt(pos);
            _id = cacher->GetAt(pos);
        }
        else
        {
            _id = AllocId();
            cacher->GetAt(pos) = _id;
        }
    }
    inline const V& Get() const { return *_v; }
    inline IdType GetId() const { return _id; }

    static inline Cacher* GetCacher();
private:
    SharedConstV _v;
    IdType _id;

    static inline IdType AllocId();
};

template<typename V, int DEFAULT_CACHE_SIZE, class VTraits>
inline
typename XyFlyWeight<V, DEFAULT_CACHE_SIZE, VTraits>::Cacher* XyFlyWeight<V, DEFAULT_CACHE_SIZE, VTraits>::GetCacher()
{
    static Cacher cacher(DEFAULT_CACHE_SIZE);
    return &cacher;
}

template<typename V, int DEFAULT_CACHE_SIZE, class VTraits>
inline
typename XyFlyWeight<V, DEFAULT_CACHE_SIZE, VTraits>::IdType XyFlyWeight<V, DEFAULT_CACHE_SIZE, VTraits>::AllocId()
{
    static IdType cur_id=INVALID_ID;
    ++cur_id;
    if (cur_id==INVALID_ID)
    {
        ++cur_id;
    }
    return cur_id; 
}

typedef XyFlyWeight<CStringW, 16*1024, CStringElementTraits<CStringW>> XyFwStringW;

static inline std::size_t hash_value(const double& d)
{
    std::size_t hash = 515;
    const int32_t* tmp = reinterpret_cast<const int32_t*>(&d);
    hash += (hash<<5);
    hash += *(tmp++);
    hash += (hash<<5);
    hash += *(tmp++);
    return hash;
}

static inline std::size_t hash_value(const CStringW& s)
{
    return CStringElementTraits<CStringW>::Hash(s);
}

static inline std::size_t hash_value(const CRect& s)
{
    std::size_t hash = 515;
    hash += (hash<<5);
    hash += s.left;
    hash += (hash<<5);
    hash += s.right;
    hash += (hash<<5);
    hash += s.bottom;
    hash += (hash<<5);
    hash += s.top;
    return hash;
}

struct XyHashValue
{
    template<typename T>
    std::size_t operator()(const T& v) const { return hash_value(v); }
};

// Interns values of T: equal values share one immutable copy, which is freed
// along with its last handle. Handles to equal values therefore compare equal.
// T needs hash_value() and operator==. Not thread-safe.
template<typename T>
class XyInterned
{
public:
    XyInterned() : m_value(Intern(T())) {}
    XyInterned(const T& v) : m_value(Intern(v)) {}
    XyInterned& operator=(const T& v) { m_value = Intern(v); return *this; }

    const T& get() const { return *m_value; }
    operator const T&() const { return *m_value; }

    friend bool operator==(const XyInterned& a, const XyInterned& b) { return a.m_value == b.m_value; }
    friend bool operator!=(const XyInterned& a, const XyInterned& b) { return a.m_value != b.m_value; }

    // Number of distinct values currently interned.
    static std::size_t Count() { return GetTable().size(); }

private:
    struct Hash
    {
        std::size_t operator()(const T* v) const { return XyHashValue()(*v); }
    };
    struct Equal
    {
        bool operator()(const T* a, const T* b) const { return std::equal_to<T>()(*a, *b); }
    };
    typedef std::unordered_map<const T*, std::weak_ptr<const T>, Hash, Equal> Table;

    // Never destroyed, so handles in static objects can outlive it at exit.
    static Table& GetTable()
    {
        static Table* table = new Table;
        return *table;
    }

    static std::shared_ptr<const T> Intern(const T& v)
    {
        Table& table = GetTable();
        typename Table::iterator it = table.find(&v);
        if (it != table.end()) {
            if (std::shared_ptr<const T> existing = it->second.lock()) {
                return existing;
            }
            table.erase(it);
        }
        std::shared_ptr<const T> value(new T(v), [](const T* p) {
            Table& table = GetTable();
            typename Table::iterator it = table.find(p);
            if (it != table.end() && it->first == p) {
                table.erase(it);
            }
            delete p;
        });
        table.emplace(value.get(), value);
        return value;
    }

    std::shared_ptr<const T> m_value;
};

// Builds a V from each distinct key K once and shares it while any handle to
// it exists. K needs hash_value() and operator==, and V a constructor taking
// const K&. Not thread-safe.
template<typename K, typename V>
class XyInternedByKey
{
public:
    template<typename A>
    explicit XyInternedByKey(const A& key) : m_value(Get(K(key))) {}

    const V& get() const { return *m_value; }
    operator const V&() const { return *m_value; }

    // Number of distinct keys currently with a value.
    static std::size_t Count() { return GetTable().size(); }

private:
    struct Entry
    {
        std::weak_ptr<const V> value;
        const V* raw;
    };
    typedef std::unordered_map<K, Entry, XyHashValue> Table;

    // Never destroyed, so handles in static objects can outlive it at exit.
    static Table& GetTable()
    {
        static Table* table = new Table;
        return *table;
    }

    static std::shared_ptr<const V> Get(const K& key)
    {
        Table& table = GetTable();
        typename Table::iterator it = table.find(key);
        if (it != table.end()) {
            if (std::shared_ptr<const V> existing = it->second.value.lock()) {
                return existing;
            }
            table.erase(it);
        }
        std::shared_ptr<const V> value(new V(key), [key](const V* p) {
            Table& table = GetTable();
            typename Table::iterator it = table.find(key);
            if (it != table.end() && it->second.raw == p) {
                table.erase(it);
            }
            delete p;
        });
        Entry entry = { value, value.get() };
        table.emplace(key, entry);
        return value;
    }

    std::shared_ptr<const V> m_value;
};

typedef XyInterned<CRect> FwRect;
