#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace vr::gameplay::weapons
{
 // Render packets borrow descriptors until the asset unload/drain boundary.
 // Hash lookup avoids scanning every ammo-count variant; individual allocations
 // keep descriptors and their inline group arrays stable across table growth.
 template<class Value,size_t Budget=16*1024*1024>class immutable_surface_cache
 {
  struct key {const void* source;std::uint32_t mask;bool operator==(const key& b)const noexcept{return source==b.source && mask==b.mask;}};
  struct hash {size_t operator()(key k)const noexcept{return std::hash<const void*>{}(k.source)^(size_t(k.mask)*0x9e3779b9u);}};
  std::unordered_multimap<key,std::unique_ptr<Value>,hash> entries_;
 public:
  static constexpr size_t capacity=Budget/(sizeof(Value)+64); // Include map-node overhead in the bound.
  size_t size()const noexcept{return entries_.size();}
  void clear()noexcept{entries_.clear();} // Only after native consumers have drained.
  template<class Match,class Build>const Value* get(const void* source,std::uint32_t mask,Match&& match,Build&& build)
  {
   const key k{source,mask};const auto range=entries_.equal_range(k);
   for(auto i=range.first;i!=range.second;++i)if(match(*i->second))return i->second.get();
   if(entries_.size()>=capacity)return nullptr;
   auto value=std::make_unique<Value>();build(*value);const auto* result=value.get();
   entries_.emplace(k,std::move(value));return result;
  }
 };
}
