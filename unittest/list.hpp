#pragma once
#include <timestone++.hpp>
#include <cstdint>
#include <iostream>

// RULE 1. Inherit ts::ts_object
class node : public ts::ts_object {
public:
	int64_t val;
	// RULE 2. Use ts::ts_persistent_ptr<>
	// in the class and structure
	ts::ts_persistent_ptr<node> p_next;
};

class child_node : public node {
	// Empty child node for testing
};

class list : public ts::ts_object {
private:
	ts::ts_persistent_ptr<node> p_head;
	enum {
		val_min = INT64_MIN,
		val_max = INT64_MAX,
	};

public:
	list(void) {
		ts::ts_cached_ptr<node> p_max, p_min;

		p_max = new node;
		p_max->val = val_max;
		p_max->p_next = nullptr;

		p_min = new node;
		p_min->val = val_min;
		// RULE 7. The conversion between
		// ts_persistent_ptr<> and ts_cached_ptr<>
		// is automatic so you don't need to
		// worry.
		p_min->p_next = p_max;

		p_head = p_min;
	}

	bool add(int64_t val) {
		// RULE 3. Use ts::ts_cached_ptr()
		// in the function
		ts::ts_cached_ptr<node> p_prev, p_next;
		int64_t v;
		bool result;

		p_prev = p_head;
		p_next = p_prev->p_next;

		if (p_prev == nullptr || p_next == nullptr) {
			return false;
		}

		while (true) {
			v = p_next->val;
			if (v >= val) {
				break;
			}
			p_prev = p_next;
			p_next = p_prev->p_next;
		}
		result = (v != val);

		if (result) {
			ts::ts_lock(p_prev);
			ts::ts_lock(p_next);

			ts::ts_cached_ptr<node> p_new_node;
			// RULE 4. Use new operator
			// to allocate ts_object as usual
			// C++ program. Note that we do not
			// support array allocation yet.
			p_new_node = new node;
			p_new_node->val = val;
			p_new_node->p_next = p_next;
			p_prev->p_next = p_new_node;
		}

		return result;
	}

	bool remove(int64_t val) {
		ts::ts_cached_ptr<node> p_prev, p_next;
		int64_t v;
		bool result;

		p_prev = p_head;
		p_next = p_prev->p_next;

		while (true) {
			v = p_next->val;
			if (v >= val) {
				break;
			}
			p_prev = p_next;
			p_next = p_prev->p_next;
		}
		result = (v == val);

		if (result) {
			ts::ts_cached_ptr<node> p_node;
			p_node = p_next->p_next;

			// RULE 5. Use ts_lock() or
			// ts_lock_const(). try_lock() is
			// hidden under the c++ wrapper.
			ts::ts_lock(p_prev);
			ts::ts_lock_const(p_next);

			p_prev->p_next = p_node;
			// RULE 6. Use delete operator
			// to allocate ts_object as usual
			// C++ program. Note that we do not
			// support array allocation yet.
			delete p_next;
		}

		return result;
	}

	bool contains(int64_t val) {
		int64_t v;
		ts::ts_cached_ptr<node> p_prev;

		p_prev = p_head;
		while (p_prev != nullptr) {
			v = p_prev->val;
			if (v == val)
				return true;
			p_prev = p_prev->p_next;
		}
		return false;
	}

	int size(void) {
		int size = 0;
		ts::ts_cached_ptr<node> p_next, p_prev;

		p_prev = p_head->p_next;
		p_next = p_prev->p_next;
		while (p_next != nullptr) {
			size++;
			p_prev = p_next;
			p_next = p_prev->p_next;
		}
		return size;
	}

	void print(void) {
		int64_t v;
		ts::ts_cached_ptr<node> p_prev;
		ts::ts_cached_ptr<child_node> p_child_prev;
		int seq;

		seq = 0;
		p_prev = p_head;
		std::cout << "[### LIST]";
		while (p_prev != nullptr) {
			if (p_prev->val != val_min &&
			    p_prev->val != val_max) {
				// RULE 20. For ts::ts_cached_ptr<>, you can do
				// type casting with ts::static_pointer_cast<>,
				// ts::dynamic_pointer_cast<>, ts::const_pointer_cast<>,
				// and ts::reinterpret_pointer_cast<>. The usage of these
				// type casting functions are the same as ones in std C++.
				//
				// See details in below:
				// https://en.cppreference.com/w/cpp/memory/shared_ptr/pointer_cast
				p_child_prev = ts::static_pointer_cast<child_node>(p_prev);
				v = p_prev->val;
				std::cout << " " << v;
				seq ++;
			}
			p_prev = p_prev->p_next;
		}
		std::cout << std::endl;
	}
};
