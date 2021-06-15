#pragma once
#include <timestone++.hpp>
#include <cstdint>
#include <iostream>

class entry : public ts::ts_object {
public:
		int64_t val;
};

class static_array {
private:
		ts::ts_persistent_array<entry, 10> array;
public:
		static_array() {}

		bool add_at(int index, int64_t _val) {

			// create a cached pointer to write the data
			// and lock the index
			ts::ts_cached_ptr<entry> p_data;
			bool result;
			
			// get the right version of the index that is going to be updated
			// then lock this entry to avoid concurrent updates
			// here persistent_ptr -> raw pointer
			// assigning raw_ptr to cached_ptr will trigger ts_deref
	
//			TODO:p_data = array[index]; //compile error //p_data = *(array + index);
			p_data = &array[index]; // p_data = (array + index);
			ts::ts_lock(p_data);

			// write the data to cached pointer
			p_data->val = _val;

			// assign the modified cached pointer to the array at the index
			// here the cached pointer -> raw pointer
			// assigning raw pointer to persistent pointer will trigger ts_assign_ptr
			array[index] = p_data;
			return true;
		}

		int64_t read_at(int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			
			std::cout << "=======================" << std::endl;
			// get the data in cached pointer
			// here the persistent pointer is converted to raw pointer
			// raw pointer is dereferenced and then assigned to cached pointer
			p_data = array[index];
			_val = p_data->val;
			std::cout << " val at index " << index << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_plus(unsigned int index, int incr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr = (p_ptr + incr);
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index + incr << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_minus(unsigned int index, int decr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr = (p_ptr - decr);
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index - decr << " = "<< _val << std::endl;
			return _val;
		}
		
		int64_t	test_operator_peq(unsigned int index, int incr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr += incr;
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index + incr << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_meq(unsigned int index, int decr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr -= decr;
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index - decr << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_incr(unsigned int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;
			ts::ts_persistent_ptr<entry> p_ptr1;

			std::cout << "=======================" << std::endl;
			std::cout << "input index = " << index << std::endl;
			p_ptr = array[index];
			p_ptr1 = ++p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " prefix++: val at index " << index + 1 << " = " << _val << std::endl;
			
		/*	TODO: need to check the postfix expr
			p_ptr1 = p_ptr++;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " postfix++: val at index " << index + 1 << " = " << _val << std::endl;*/
			return _val;
		}

		int64_t	test_operator_decr(unsigned int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;
			ts::ts_persistent_ptr<entry> p_ptr1;

			std::cout << "=======================" << std::endl;
			std::cout << "input index = " << index << std::endl;
			p_ptr = array[index];
			p_ptr1 = --p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " prefix--: val at index " << index - 1 << " = " << _val << std::endl;

			/*	TODO: need to check the postfix expr
			p_ptr1 = p_ptr--;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " postfix--: val at index " << index - 1 << " = " << _val << std::endl;*/
			return _val;
		}

		int64_t	test_pointer_arithmetic(unsigned int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;
			ts::ts_persistent_ptr<entry> p_ptr1;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			
			p_ptr = (p_ptr + 1);
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr = (p_ptr - 1);
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr += 1;
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr -= 1;
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr1 = --p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;

			p_ptr1 = ++p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;

			/*p_ptr1 = p_ptr--;
			p_data = p_ptr1;
			_val = p_data->val;

			p_ptr1 = p_ptr++;
			p_data = p_ptr1;
			_val = p_data->val;*/

			p_data = p_ptr;
			_val = p_data->val;

			std::cout << " val at index " << index << " = "<< _val << std::endl;
			std::cout << "=======================" << std::endl;
			return _val;
		}

		void print() {
			ts::ts_cached_ptr<entry> p_data;
			int64_t _val;
			
			std::cout << "=======================" << std::endl;
			std::cout << "[### ARRAY]";
			for (auto i = 0; i < 10; ++i) {
				p_data = array[i];
				if (!p_data) {
					std::cout << std::endl;
					return;
				}
				_val = p_data->val;
				std::cout << " " << _val;
			}
			std::cout << std::endl;
			return;
		}
};

class dynamic_array {
private:
		ts::ts_persistent_dynamic_array<entry> array;
		int n_elem;
public:
		dynamic_array(int n) { 
			n_elem = n;
			array.init(n_elem);
		}

		bool add_at(int index, int64_t _val) {

			ts::ts_cached_ptr<entry> p_data;
			bool result;
				
//			TODO:p_data = array[index]; //compile error //p_data = *(array + index);
			p_data = &array[index]; // p_data = (array + index);
			ts::ts_lock(p_data);

			// write the data to cached pointer
			p_data->val = _val;

			// assign the modified cached pointer to the array at the index
			// here the cached pointer -> raw pointer
			// assigning raw pointer to persistent pointer will trigger ts_assign_ptr
			array[index] = p_data;
			return true;
		}

		int64_t read_at(int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			
			std::cout << "=======================" << std::endl;
			// get the data in cached pointer
			// here the persistent pointer is converted to raw pointer
			// raw pointer is dereferenced and then assigned to cached pointer
			p_data = array[index];
			_val = p_data->val;
			std::cout << " val at index " << index << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_plus(unsigned int index, int incr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr = (p_ptr + incr);
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index + incr << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_minus(unsigned int index, int decr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr = (p_ptr - decr);
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index - decr << " = "<< _val << std::endl;
			return _val;
		}
		
		int64_t	test_operator_peq(unsigned int index, int incr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr += incr;
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index + incr << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_meq(unsigned int index, int decr) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			p_ptr -= decr;
			p_data = p_ptr;
			_val = p_data->val;
			std::cout << " val at index " << index - decr << " = "<< _val << std::endl;
			return _val;
		}

		int64_t	test_operator_incr(unsigned int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;
			ts::ts_persistent_ptr<entry> p_ptr1;

			std::cout << "=======================" << std::endl;
			std::cout << "input index = " << index << std::endl;
			p_ptr = array[index];
			p_ptr1 = ++p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " prefix++: val at index " << index + 1 << " = " << _val << std::endl;

		/*	p_ptr1 = p_ptr++;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " postfix++: val at index " << index + 1 << " = " << _val << std::endl;*/
			return _val;
		}

		int64_t	test_operator_decr(unsigned int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;
			ts::ts_persistent_ptr<entry> p_ptr1;

			std::cout << "=======================" << std::endl;
			std::cout << "input index = " << index << std::endl;
			p_ptr = array[index];
			p_ptr1 = --p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " prefix--: val at index " << index - 1 << " = " << _val << std::endl;

		/*	p_ptr1 = p_ptr--;
			p_data = p_ptr1;
			_val = p_data->val;
			std::cout << " postfix--: val at index " << index - 1 << " = " << _val << std::endl;*/
			return _val;
		}

		int64_t	test_pointer_arithmetic(unsigned int index) {

			int64_t _val;
			// create a cached pointer to retrieve the data
			ts::ts_cached_ptr<entry> p_data;
			ts::ts_persistent_ptr<entry> p_ptr;
			ts::ts_persistent_ptr<entry> p_ptr1;

			std::cout << "=======================" << std::endl;
			p_ptr = array[index];
			
			p_ptr = (p_ptr + 1);
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr = (p_ptr - 1);
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr += 1;
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr -= 1;
			p_data = p_ptr;
			_val = p_data->val;

			p_ptr1 = --p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;

			p_ptr1 = ++p_ptr;
			p_data = p_ptr1;
			_val = p_data->val;

		/*	p_ptr1 = p_ptr--;
			p_data = p_ptr1;
			_val = p_data->val;

			p_ptr1 = p_ptr++;
			p_data = p_ptr1;
			_val = p_data->val;*/

			p_data = p_ptr;
			_val = p_data->val;

			std::cout << " val at index " << index << " = "<< _val << std::endl;
			std::cout << "=======================" << std::endl;
			return _val;
		}

		void print() {
			ts::ts_cached_ptr<entry> p_data;
			int64_t _val;
			
			std::cout << "=======================" << std::endl;
			std::cout << "[### ARRAY]";
			for (auto i = 0; i < 10; ++i) {
				p_data = array[i];
				if (!p_data) {
					std::cout << std::endl;
					return;
				}
				_val = p_data->val;
				std::cout << " " << _val;
			}
			std::cout << std::endl;
			return;
		}
};
