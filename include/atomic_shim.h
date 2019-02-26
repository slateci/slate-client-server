#ifndef SLATE_ATOMIC_SHIM_H
#define SLATE_ATOMIC_SHIM_H

#include <mutex>
#include <mutex>

template<typename T>
class simple_atomic{
public:
	//nothing clever for construction or destruction
	//these must happen in a single-threaded manner
	simple_atomic()=default;
	simple_atomic(T val):t(val){}
	simple_atomic(const simple_atomic<T>&)=delete;
	~simple_atomic()=default;
	
	//note: weaker exception gaurantee than std::atomic
	T operator=(const T& other){
		std::lock_guard<std::mutex> lck(mut);
		if(&other!=&t)
			t=other;
		return t;
	}
	simple_atomic<T> operator=(const simple_atomic<T>& other)=delete;
	
	//this implementation is never lock-free
	bool is_lock_free() const noexcept{
		return false;
	}
	
	//note: weaker exception gaurantee than std::atomic
	//memory order parameter is ignored
	void store(T val, std::memory_order sync = std::memory_order_seq_cst){
		std::lock_guard<std::mutex> lck(mut);
		t=val;
	}
	
	//note: weaker exception gaurantee than std::atomic
	//memory order parameter is ignored
	T load(std::memory_order sync = std::memory_order_seq_cst) const{
		T val;
		{
			std::lock_guard<std::mutex> lck(mut);
			val=t;
		}
		return val;
	}
	
	//note: weaker exception gaurantee than std::atomic
	operator T() const{
		T val;
		{
			std::lock_guard<std::mutex> lck(mut);
			val=t;
		}
		return val;
	}
	
	//note: weaker exception gaurantee than std::atomic
	//memory order parameter is ignored
	T exchange(T val, std::memory_order sync = std::memory_order_seq_cst) const{
		T ret;
		{
			std::lock_guard<std::mutex> lck(mut);
			ret=t;
			t=val;
		}
		return ret;
	}
	
	//note: weaker exception gaurantee than std::atomic
	//memory order parameter is ignored
	bool compare_exchange_strong(T& expected, T val,
	                             std::memory_order sync = std::memory_order_seq_cst){
		std::lock_guard<std::mutex> lck(mut);
		if(t==expected){
			t=val;
			return true;
		}
		return false;
	}
private:
	T t;
	mutable std::mutex mut;
};

#endif //SLATE_ATOMIC_SHIM_H
