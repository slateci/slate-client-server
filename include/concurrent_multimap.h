#ifndef SLATE_CONCURRENT_MULTIMAP_H
#define SLATE_CONCURRENT_MULTIMAP_H

#include <functional>
#include <unordered_set>

#include <libcuckoo/cuckoohash_map.hh>

///Implements a multimap by storing items within unordered sets indexed by the 
///keys. This requires not only the keys but the values as well to be hasable 
///and equality comparable. 
///Suports concurrent access to the extent that operations on different buckets
///in the underlying cuckoohash_map can proceed concurrently, however, operations
///involving different values with the same key are guaranteed to map to the same
///bucket and thus will block each other waiting for its lock. 
///Does not currently have allocation or iteration support.
template<typename Key, typename Value, 
         typename KeyHash=std::hash<Key>, typename KeyEqual=std::equal_to<Key>, 
         typename ValueHash=std::hash<Value>, typename ValueEqual=std::equal_to<Value>>
class concurrent_multimap{
public:
	using steady_clock=std::chrono::steady_clock;
	///The collection of values to which a key maps
	using set_type=std::unordered_set<Value,ValueHash,ValueEqual>;
	///The set of values the key maps to with its associated expiration time
	using category_type=std::pair<set_type,steady_clock::time_point>;
	///The underlying hash table type
	using Table=cuckoohash_map<Key,category_type,KeyHash,KeyEqual>;
	using key_type=Key;
	using mapped_type=Value;
	using value_type=std::pair<const Key,category_type>;
	using size_type=typename Table::size_type;

	concurrent_multimap(){}
	///If other is being modified concurrently, behavior is unspecified.
	concurrent_multimap(const concurrent_multimap& other):data(other.data){}
	///If other is being modified concurrently, behavior is unspecified.
	concurrent_multimap(concurrent_multimap&& other):data(std::move(other.data)){}
	concurrent_multimap(std::initializer_list<value_type> init):data(std::move(init)){}
	~concurrent_multimap(){}
	
	///If this or other is being modified concurrently, behavior is unspecified.
	concurrent_multimap& operator=(const concurrent_multimap& other){
		if(&other!=this)
			data=other.data;
		return *this;
	}
	///If this or other is being modified concurrently, behavior is unspecified.
	concurrent_multimap& operator=(concurrent_multimap&& other){
		if(&other!=this)
			data=std::move(other.data);
		return *this;
	}
	
	//concurrency-safe access and manipulation functions
	
	///Removes all elements in the table, calling their destructors.
	void clear(){ data.clear(); }
	
	///Reserve enough space in the table for the given number of elements. 
	///If the table can already hold that many elements, the function will 
	///shrink the table to the smallest hashpower that can hold the maximum of 
	///the specified amount and the current table size.
	///\param n	the number of elements to reserve space for
	///\return true if the size of the table changed, false otherwise
	bool reserve(size_type n){ return data.reserve(n); }
	
	///Resizes the table to the given hashpower. If this hashpower is not larger 
	///than the current hashpower, then it decreases the hashpower to the 
	///maximum of the specified value and the smallest hashpower that can hold 
	///all the elements currently in the table.
	///\param n	the hashpower to set for the table
	///\return true if the table changed size, false otherwise
	bool rehash(size_type n){ return data.rehash(n); }
	
	///Erases the key from the table. 
	///\tparam K type of the key
	///\param k the key to be removed
	///\return the number of items to which the key mapped which were removed
	template <typename K>
	size_type erase(const K& k){
		size_type erased=0;
		data.erase_fn(k,[&erased](const category_type& cat){
			erased=cat.first.size();
			return true;
		});
		return erased;
	}
	
	///Erases the mapping of the key to a single value from the table, leaving
	///any other values to which that key may map. 
	///\tparam K type of the key
	template <typename K>
	size_type erase(const K& k, const mapped_type& v){
		size_type erased=0;
		data.erase_fn(k,[&erased,&v](category_type& cat){
			erased=cat.first.erase(v);
			return cat.first.empty(); //only erase whole category if empty
		});
		return erased;
	}
	
	///Searches the table for \p k and returns the associated value it
	///finds. @c mapped_type must be @c CopyConstructible.
	///\tparam K type of the key
	///\param k the key for which to search
	///\return the collection of values associated with the key, or any empty 
	///        collection if the key is not found
	template <typename K>
	category_type find(const K& key) const{
		category_type items;
		data.find_fn(key,[&items](const category_type& cat){ items=cat;	});
		return items;
	}
	
	///Inserts the key-value pair into the table. If the pair is already in the 
	///table, the version of \p val which was laready present is replaced with 
	///the new one. 
	///\tparam K type of the key
	///\param key the key for which to search
	///\param val the value for which to search
	///\return true if the key was newly inserted, false if the key was already 
	///        in the table
	template<typename K, typename V>
	bool insert_or_assign(K&& key, V&& val){
		bool inserted=true;
		//Unfortunately, since val does not match up with Table::mapped_type we 
		//need to preemptively construct an entire category_type object which
		//may be unneeded.
		data.upsert(std::forward<K>(key), 
					[&](category_type& cat){
						if(cat.first.count(val)){
							inserted=false;
							//ensure replacement
							cat.first.erase(val);
						}
						cat.first.emplace(val);
			    },category_type({val}, steady_clock::now()));
		return inserted;
	}
	
	///Inserts the key-value pair into the table.
	///\returns true if the pair was newly inserted, false if it was already present
	template<typename K, typename V>
	bool insert(K&& key, V&& val){
		bool inserted=true;
		//Unfortunately, since val does not match up with Table::mapped_type we 
		//need to preemptively construct an entire category_type object which
		//may be unneeded.
		data.upsert(std::forward<K>(key), 
					[&](category_type& cat){
						inserted=cat.first.emplace(val).second;
			    },category_type({mapped_type{val}}, steady_clock::now()));
		return inserted;
	}
	
	///Updates the key-value pair in the table. If the pair is already in the 
	///table, the version of \p val which was laready present is replaced with 
	///the new one, otherwise does nothing.
	///\tparam K type of the key
	///\tparam V type of the value
	///\param key the key for which to search
	///\param val the value for which to search
	///\return true if the entry was updated, false if it was not found
	template<typename K, typename V>
	bool update(K&& key, V&& val){
		bool updated=false;
		data.update_fn(key,[&](category_type& cat){
			if(cat.first.count(val)){
				updated=true;
				//ensure replacement
				cat.first.erase(val);
				cat.first.emplace(val);
			}
		});
		return updated;
	}

	///Updates the expiration time of a category associated with \p key
	///\tparam K type of the key
	///\param key the key for which to update expiration time
	///\param time the expiration time to update to
	///\return true is the expiration time was updated, false if the key was not found
	template <typename K>
	bool update_expiration(K&& key, steady_clock::time_point time){
		bool updated=false;
		data.update_fn(key,[&](category_type& cat){
		    cat.second = time;
		    updated=true;
		});
		return updated;
	}

	///Returns whether or not \p key is in the table.
	///\tparam K type of the key
	///\param k the key for which to search
	///\return true if the key maps to at least one value in the table
	template <typename K>
	bool contains(const K& key) const{
		return data.contains(key);
	}
	
	///Returns whether or not the pair \p key -> \p val is in the table.
	///\tparam K type of the key
	///\tparam V type of the value
	///\param key the key for which to search
	///\param val the value for which to search
	///\return true if the key,value pair is in the table
	template <typename K, typename V>
	bool contains(const K& key, V&& val) const{
		bool found=false;
		data.find_fn(key,[&](const category_type& cat){ found=cat.first.count(val); });
		return found;
	}
	
	///Get the number of values associated with a key
	///\tparam K type of the key
	///\param k the key for which to search
	///\return the number of values associated with \p key; 0 if it is not in 
	///        the map
	template <typename K>
	size_type count(const K& k) const{
		size_type n=0;
		data.find_fn(k,[&n](const category_type& cat){ n=cat.first.size(); });
		return n;
	}
	
	///Get the number of occurances of a key,value pair
	///\tparam K type of the key
	///\tparam V type of the value
	///\param k the key for which to search
	///\param v the value for which to search
	///\return 1 if the pair is associated in the map, zero if not
	template <typename K, typename V>
	size_type count(const K& k, V&& v) const{
		size_type n=0;
		data.find_fn(k,[&](const category_type& cat){ n=cat.first.count(v); });
		return n;
	}
	
	///Returns whether or not the pair \p key -> \p val is in the table.
	///\tparam K type of the key
	///\tparam V type of the value
	///\param key the key for which to search
	///\param val the value for which to search
	///\return true if the key,value pair is in the table
	template <typename K, typename V>
	bool find(const K& key, V&& val) const{
		bool found=false;
		data.find_fn(key,[&](const category_type& cat){
			found=cat.first.count(val);
			if(found)
				val=*cat.first.find(val);
		});
		return found;
	}

private:
	Table data;
};

#endif //SLATE_CONCURRENT_MULTIMAP_H
