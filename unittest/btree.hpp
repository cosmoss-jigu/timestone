#pragma once 
#include <timestone++.hpp>
#include <cstdint>
#include <iostream>


using namespace ts;
#define FAN_OUT 5
#define MAX_KEYS FAN_OUT - 1 
#define MAX_PTRS FAN_OUT

class btree_node : public ts_object {
public:
		ts_persistent_array<int64_t, MAX_KEYS> key;
		ts_persistent_array<void*, MAX_PTRS> ptr;
		ts_persistent_array<void*, 1> parent;
		ts_persistent_array<int, 1> num_keys;
		bool is_leaf;
		int id;
};

class btree : public ts_object  {
private:
		ts_persistent_ptr<btree_node> root;
public:
		btree(void) {
			ts_cached_ptr<btree_node> dummy, node, temp;
			ts_cached_ptr<int64_t> p_key;
			ts_cached_ptr<void *> pp_val, _pp_node;
			ts_cached_ptr<btree_node*> pp_node;

			dummy = create_node();
			node = create_leaf();
			for (int i = 0; i < MAX_KEYS; ++i) {
				p_key = &dummy->key[i];
				pp_val = &dummy->ptr[i];	
				*p_key = INT64_MIN;
				*pp_val = NULL;
				dummy->key[i] = p_key;
				dummy->ptr[i] = pp_val;
			}
			_pp_node = &dummy->ptr[MAX_PTRS - 1];
			*_pp_node = node;
			dummy->ptr[MAX_PTRS - 1] = _pp_node;
			root = dummy;
/*			std::cout<< "dummy= " << (void *) dummy << std::endl;
			std::cout<< "root= " << (void *) root << std::endl;
			std::cout<< "node= " << (void *) node << std::endl;*/
	}

		ts_cached_ptr<btree_node> create_node() {
			ts_cached_ptr<btree_node> node;	
			ts_cached_ptr<int> p_num_keys;
			ts_cached_ptr<void*> pp_parent;
			static int count;

			node = new btree_node;
			node->is_leaf = false;
			p_num_keys = &node->num_keys[0];
			*p_num_keys = 0;
			node->num_keys[0] = p_num_keys;
			pp_parent = &node->parent[0];
			*pp_parent = NULL;
			node->parent[0] = pp_parent;
			node->id = count;
			++count;
			return node;
		}

		ts_cached_ptr<btree_node> create_leaf() {
			ts_cached_ptr<btree_node> leaf;
			ts_cached_ptr<int> p_num_keys;
			ts_cached_ptr<void*> pp_parent;
			static int count = 0;

			leaf = new btree_node;
			leaf->is_leaf = true;
			p_num_keys = &leaf->num_keys[0];
			*p_num_keys = 0;
			leaf->num_keys[0] = p_num_keys;
			pp_parent = &leaf->parent[0];
			*pp_parent = NULL;
			leaf->parent[0] = pp_parent;
			leaf->id = count;
			++count;
			return leaf;
		}

		ts_cached_ptr<btree_node> get_leaf(ts_cached_ptr<btree_node> 
				root, int64_t target_key) {
			
			int i = 0, num_keys;
			ts_cached_ptr<btree_node> node = root;
			ts_cached_ptr<int64_t> p_key;
			ts_cached_ptr<int> p_num_keys;
			ts_cached_ptr<void*> pp_next_node;
			int64_t key;

//			std::cout << "starting from node " << node->id << std::endl; 
			while(!node->is_leaf) {
				i = 0;
				p_num_keys = &node->num_keys[0];
				num_keys = *p_num_keys;
//				std::cout << "node= " << (void *) node << "\t" << "num_keys= " << num_keys << std::endl;
				while (i < num_keys) {
					p_key = &node->key[i];
					key = *p_key;
					if (target_key >= key) {
						++i;
					}
					else
						break;
				}
				/* assigning void* ptr to cached ptr
				* void* -> btree_node* 
				* then deref the right version of the sibling*/
				pp_next_node = &node->ptr[i];
				node = *pp_next_node;
			}
			return node;
		}

		bool insert(int64_t key, char* val) {
			ts_cached_ptr<btree_node> _root, new_root, leaf;
			ts_cached_ptr<void*> pp_root;
			ts_cached_ptr<int> p_num_keys;
			int num_keys;
			
			/* use pp_root for ts_lock when modifying root pointer*/
			pp_root = &root->ptr[MAX_PTRS - 1];
			_root = *pp_root; 
			
//			_root = root->ptr[MAX_PTRS - 1];
//			std::cout<< "_root= " << (void *) _root << std::endl;
			leaf = get_leaf(_root, key);
			p_num_keys = &leaf->num_keys[0];
			num_keys = *p_num_keys;
			if (num_keys < MAX_KEYS) {
				insert_into_leaf(leaf, key, val);
				return true;
			}
		//	std::cout << " ### Leaf node split ###" << std::endl;
			new_root = split_insert_leaf(_root, leaf, key, val);
			if (_root == new_root) {
				return true;
			}
			else{
				ts_lock(pp_root);
				*pp_root = new_root;
				root->ptr[MAX_PTRS - 1] = pp_root;
			//	std::cout << "### node split changed root ###" << std::endl;
			//	std::cout<< "### new_root= " << (void *) new_root << " ###" <<std::endl;
			}
			return true;
		}

		void insert_into_leaf(ts_cached_ptr<btree_node> leaf, 
				int64_t input_key, char *val) {

			int i, num_keys, slot = 0;
			ts_cached_ptr<int> p_num_keys;
			ts_cached_ptr<int64_t> p_key, p_prev_key;
			ts_cached_ptr<char *> pp_val;
			ts_cached_ptr<void *> pp_prev_val, pp_curr_val, _pp_val;
			int64_t key, prev_key;
			void *prev_val;

			p_num_keys = &leaf->num_keys[0];
			num_keys = *p_num_keys;
			ts_lock(p_num_keys);
			while (slot < num_keys) {	
				p_key = &leaf->key[slot];
				key = *p_key;
				if (key < input_key) {
					++slot;
				}
				else
					break;
			}
			for (i = num_keys; i > slot; --i) {
				p_prev_key = &leaf->key[i - 1];
				prev_key = *p_prev_key;
				pp_prev_val = &leaf->ptr[i - 1];
				prev_val = *pp_prev_val;
				
				/* updating key*/
				p_key = &leaf->key[i];
				ts_lock(p_key);
				*p_key = prev_key;
				leaf->key[i] = p_key;

				/* update the val*/
				pp_curr_val = &leaf->ptr[i];
				ts_lock(pp_curr_val);
				*pp_curr_val = prev_val;
				leaf->ptr[i] = pp_curr_val;
			}
			/* insert new key*/
			p_key = &leaf->key[slot];
			ts_lock(p_key);
			*p_key = input_key;
			leaf->key[slot] = p_key;

			/* insert new val*/
			_pp_val = &leaf->ptr[slot];
			ts_lock(_pp_val);
			*_pp_val = val;
			leaf->ptr[slot] = _pp_val;
			
			/* update num_keys*/
			++num_keys;
//			ts_lock(p_num_keys);
			*p_num_keys = num_keys;
			leaf->num_keys[0] = p_num_keys;
		}
		
		ts_cached_ptr<btree_node> split_insert_leaf(ts_cached_ptr<btree_node> root, 
				ts_cached_ptr<btree_node> leaf, int64_t input_key, char *input_val) {
			
			ts_cached_ptr<void*> pp_sibling, pp_sibling_new; 
			ts_cached_ptr<void*> pp_val, pp_val_new, pp_parent;
			ts_cached_ptr<void *> pp_prev_val, pp_curr_val, pp_parent_new;
			ts_cached_ptr<btree_node> sibling, new_leaf, new_root;
			ts_cached_ptr<int> p_num_keys;
			ts_cached_ptr<int64_t> p_key, p_key_new, p_prev_key;
			int slot = 0, n_keys_old = 0, n_keys_new = 0, num_keys;
			int split, i, j;
			int64_t key, prev_key;
			void *val, *prev_val, *parent, *sibling_new;
			bool insert_old_leaf = false, insert_new_leaf = false;

			pp_sibling = &leaf->ptr[MAX_PTRS - 1];
			sibling = *pp_sibling; 
			/* lock the sibling before split
			 * this will prevent concurrent splits*/
			ts_lock(pp_sibling);
			p_num_keys = &leaf->num_keys[0];
			num_keys = *p_num_keys;

			/* find the slot to insert the key before split*/
			while (slot < num_keys) {
				p_key = &leaf->key[slot];
				key = *p_key;
				if (key < input_key) 
					++slot;
				else
					break;
			}
			split = cut(MAX_KEYS);
			/* if slot < split, then new key is inerted in the old leaf
			 * else insert in the new leaf*/
			if (slot < split) {
				insert_old_leaf = true;
			//	std::cout << "### key " << input_key << " will be inserted in old leaf at " << slot << " ###" <<std::endl;  
			}
			else {
				insert_new_leaf = true;
				slot = slot - split;
			//	std::cout << "### key " << input_key << " will be inserted in new leaf " << leaf->id << " at " << slot << " ###" << std::endl;
			}
			new_leaf = create_leaf();
			if (insert_new_leaf) {
				/* no need of ts_lock as the new leaf is 
				 * not yet made visible*/
				p_key = &new_leaf->key[slot];
				pp_val = &new_leaf->ptr[slot];
				*p_key = input_key;
				*pp_val = input_val;
				new_leaf->key[slot] = p_key;
				new_leaf->ptr[slot] = pp_val;
				++n_keys_new;
			}
			/*split the old leaf before inserting the key*/
			for (i = split, j = 0; i < MAX_KEYS; ++i, ++j) {
				p_key = &leaf->key[i];
				key = *p_key;
				pp_val = &leaf->ptr[i];
				val = *pp_val;
				++n_keys_old;

				if (insert_new_leaf && j == slot) 
					++j;
				p_key_new = &new_leaf->key[j];
				pp_val_new = &new_leaf->ptr[j];
				*p_key_new = key;
				*pp_val_new = val;
				new_leaf->key[j] = p_key_new;
				new_leaf->ptr[j] = pp_val_new;
				++n_keys_new;
			}

			/* update num_keys in old leaf 
			 * after inserting the input key*/
			num_keys = num_keys - n_keys_old;
			if (insert_old_leaf) {
				/* check insert into old leaf*/
				for (i = num_keys; i > slot; --i) {
					p_prev_key = &leaf->key[i - 1];
					prev_key = *p_prev_key;
					pp_prev_val = &leaf->ptr[i - 1];
					prev_val = *pp_prev_val;
				
					/* updating key*/
					p_key = &leaf->key[i];
					ts_lock(p_key);
					*p_key = prev_key;
					leaf->key[i] = p_key;

					/* update the val*/
					pp_curr_val = &leaf->ptr[i];
					ts_lock(pp_curr_val);
					*pp_curr_val = prev_val;
					leaf->ptr[i] = pp_curr_val;
				}
				/* insert new key*/
				p_key = &leaf->key[slot];
				ts_lock(p_key);
				*p_key = input_key;
				leaf->key[slot] = p_key;

				/* insert new val*/
				pp_val = &leaf->ptr[slot];
				ts_lock(pp_val);
				*pp_val = val;
				leaf->ptr[slot] = pp_val;
				++num_keys;
			}
			p_num_keys = &leaf->num_keys[0];
			ts_lock(p_num_keys);
			*p_num_keys = num_keys;
			leaf->num_keys[0] = p_num_keys;

			/* update parent for new leaf*/
			pp_parent_new = &new_leaf->parent[0];
			pp_parent = &leaf->parent[0];
			parent = *pp_parent;
			*pp_parent_new = parent;
			new_leaf->parent[0] = pp_parent_new;

			/* update the sibling pointer of old and new leaf*/
			pp_sibling_new = &new_leaf->ptr[MAX_PTRS - 1];
			sibling_new = *pp_sibling;
			*pp_sibling_new = sibling_new;
			new_leaf->ptr[MAX_PTRS - 1] = pp_sibling_new;
			*pp_sibling = new_leaf;
			leaf->ptr[MAX_PTRS - 1] = pp_sibling;
			/* insert into parent*/
			p_key_new = &new_leaf->key[0];
			key = *p_key_new;
			new_root = insert_into_parent(root, leaf, key, new_leaf, n_keys_new);
			return new_root;
		}

		int cut(int n_keys) {

			if (n_keys % 2 == 0)
				return n_keys/2;
			else
				return n_keys/2 + 1;
		}
		
		ts_cached_ptr<btree_node> insert_into_parent(ts_cached_ptr<btree_node> root,
				ts_cached_ptr<btree_node> left, int64_t key, 
				ts_cached_ptr<btree_node> right, int num_keys_right) {
			
			ts_cached_ptr<btree_node> _root, parent;
			ts_cached_ptr<void *> pp_parent, pp_node, pp_next_node;
			ts_cached_ptr<int64_t> p_key, p_curr_key;
			ts_cached_ptr<int> p_num_keys, p_num_keys_new;
			int left_index, num_keys, num_keys_old, num_keys_new, i;
			int64_t prev_key, curr_key;
			void *node, *next_node;

			pp_parent = &left->parent[0];
			parent = *pp_parent;
			if (!parent) {
				_root = insert_into_new_root(left, key, right, num_keys_right);
				/* update the num_keys in new node*/
				p_num_keys_new = &right->num_keys[0];
				*p_num_keys_new = num_keys_right;
				right->num_keys[0] = p_num_keys_new;
				return _root;
			}

			/* insert into existing parent*/
			p_num_keys = &parent->num_keys[0];
			num_keys = *p_num_keys;
			ts_lock(p_num_keys);
			left_index = get_left_index(parent, left, num_keys);
			if (num_keys < MAX_KEYS) {
				for (i = num_keys; i > left_index; --i) {
					pp_node = &parent->ptr[i];
					node = *pp_node;
					p_key = &parent->key[i - 1];
					prev_key = *p_key;
					
					pp_next_node = &parent->ptr[i + 1];
					ts_lock(pp_next_node);
					*pp_next_node = node;
					parent->ptr[i + 1] = pp_next_node;

					p_curr_key = &parent->key[i];
					ts_lock(p_curr_key);
					*p_curr_key = prev_key;
					parent->key[i] = p_curr_key;
				}
				pp_node = &parent->ptr[left_index + 1];
				ts_lock(pp_node);
				*pp_node = right;
				parent->ptr[left_index + 1] = pp_node;

				p_key = &parent->key[left_index];
				ts_lock(p_key);
				*p_key = key;
				parent->key[left_index] = p_key;
				
				/* update the num_keys in new node*/
				p_num_keys_new = &right->num_keys[0];
				*p_num_keys_new = num_keys_right;
				right->num_keys[0] = p_num_keys_new;

				++num_keys;
				*p_num_keys = num_keys;
				parent->num_keys[0] = p_num_keys;
				return root;
			}
			// split and create new parent 
			//std::cout << "### parent node full.. spliting internal node ###" << std::endl;
			_root = split_insert_into_node(root, parent, 
					left_index, p_num_keys, key, right);

			/* update the num_keys in new node*/
			p_num_keys_new = &right->num_keys[0];
			*p_num_keys_new = num_keys_right;
			right->num_keys[0] = p_num_keys_new;
			return _root;
		}
		
		ts_cached_ptr<btree_node> split_insert_into_node(ts_cached_ptr<btree_node> root,
				ts_cached_ptr<btree_node> old_node, int left_index, 
				ts_cached_ptr<int> p_num_keys, int64_t key, 
				ts_cached_ptr<btree_node> right) {
			
			ts_cached_ptr<btree_node> new_node, child, new_root;
			ts_cached_ptr<int> p_num_keys_new, p_num_keys_old;
			ts_cached_ptr<int64_t> p_key;
			ts_cached_ptr<void *> pp_node, pp_parent, pp_parent_new;
			void *temp_ptr[FAN_OUT + 1];
			void *parent;
			int64_t temp_key[FAN_OUT];
			int64_t k_prime;
			int i, j, split, num_keys, test_num_keys;
			int num_keys_old = 0, num_keys_new = 0;
			
			num_keys = *p_num_keys;
			for (i = 0, j = 0; i < num_keys + 1; ++i, ++j) {
				if (j == left_index + 1)
					++j;
				pp_node = &old_node->ptr[i];
				temp_ptr[j] = *pp_node;
			}
			for (i = 0, j = 0; i < num_keys; ++i, ++j) {
				if (j == left_index) 
					++j;
				p_key = &old_node->key[i];
				temp_key[j] = *p_key;
			}
			temp_ptr[left_index + 1] = right;
			temp_key[left_index] = key;

			split = cut(FAN_OUT);
			new_node = create_node();
			for (i = 0; i < split - 1; ++i) {
				pp_node = &old_node->ptr[i];
				ts_lock(pp_node);
				*pp_node = temp_ptr[i];
				old_node->ptr[i] = pp_node;

				p_key = &old_node->key[i];
				ts_lock(p_key);
				*p_key = temp_key[i];
				old_node->key[i] = p_key;
				++num_keys_old;
			}
			pp_node = &old_node->ptr[i];
			ts_lock(pp_node);
			*pp_node = temp_ptr[i];
			old_node->ptr[i] = pp_node;
			k_prime = temp_key[split - 1];
			/* populate the new parent*/
			for (++i, j = 0; i < FAN_OUT; ++i, ++j) {
					pp_node = &new_node->ptr[j];
					*pp_node = temp_ptr[i];
					new_node->ptr[j] = pp_node;
					p_key = &new_node->key[j];
					*p_key = temp_key[i];
					new_node->key[j] = p_key;
					++num_keys_new;
			}
			pp_node = &new_node->ptr[j];
			*pp_node = temp_ptr[i];
			new_node->ptr[j] = pp_node;

			/* update the parent ptr of the new node*/
			pp_parent = &old_node->parent[0];
			parent = *pp_parent;
			pp_parent_new = &new_node->parent[0];
			*pp_parent_new = parent;
			new_node->parent[0] = pp_parent_new; 
			/* update the parent ptr in all the children 
			 * of new_node*/
			for (int i = 0; i <= num_keys_new; ++i) {
				pp_node = &new_node->ptr[i];
				child = *pp_node;
				pp_parent = &child->parent[0];
				ts_lock(pp_parent);
				*pp_parent = new_node;
				child->parent[0] = pp_parent;
			}
			//std::cout << "### internal node split done updating the parent ###" << std::endl;
			new_root =  insert_into_parent(root, old_node, 
					k_prime, new_node, num_keys_new);

			/* update num keys in the old parent*/
			num_keys = num_keys - num_keys_old;
/*			p_num_keys_old = &old_node->num_keys[0];
			*p_num_keys_old = num_keys;
			old_node->num_keys[0] = p_num_keys_old;*/
			*p_num_keys = num_keys;
			old_node->num_keys[0] = p_num_keys;
			return new_root;
		}

		int get_left_index(ts_cached_ptr<btree_node> parent, 
				ts_cached_ptr<btree_node> left, int num_keys) {
			
			int left_index = 0;
			ts_cached_ptr<btree_node> node;
			ts_cached_ptr<void*> pp_node;

			while (left_index <= num_keys) {
				pp_node = &parent->ptr[left_index];
				node = *pp_node;
				if (node != left)
					++left_index;
				else 
					break;
			}
			return left_index;
		}

		ts_cached_ptr<btree_node> insert_into_new_root(ts_cached_ptr<btree_node> left,
				int64_t key, ts_cached_ptr<btree_node> right, int num_keys_right) {

			ts_cached_ptr<btree_node> root;
			ts_cached_ptr<btree_node> parent;
			ts_cached_ptr<int64_t> p_key;
			ts_cached_ptr<void*> pp_val, pp_parent;
			ts_cached_ptr<int> p_num_keys;
			int num_keys, num_keys_new;

			root = create_node();

			p_key = &root->key[0];
			*p_key = key;
			root->key[0] = p_key;
			++num_keys;
			pp_val = &root->ptr[0];
			*pp_val = left;
			root->ptr[0] = pp_val;
			pp_val = &root->ptr[1];
			*pp_val = right;
			root->ptr[1] = pp_val;

			p_num_keys = &root->num_keys[0];
			*p_num_keys = num_keys;
			root->num_keys[0] = p_num_keys;
			pp_parent = &root->parent[0];
			*pp_parent = NULL;
			root->parent[0] = pp_parent;

			pp_parent = &left->parent[0];
			ts_lock(pp_parent);
			*pp_parent = root;
			left->parent[0] = pp_parent;
			pp_parent = &right->parent[0];
			*pp_parent = root;
			right->parent[0] = pp_parent;
			return root;
		}

		char* lookup(int64_t target_key) {
			ts_cached_ptr<btree_node> _root, leaf;
			ts_cached_ptr<void*> pp_root;
			ts_cached_ptr<int> p_num_keys;
			ts_cached_ptr<int64_t> p_key;
			ts_cached_ptr<void *> pp_val;
			ts_cached_ptr<char *> _pp_val;
			int num_keys;
			int64_t key;
			char *val;
			
			/* use pp_root for ts_lock when modifying root pointer*/
			pp_root = &root->ptr[MAX_PTRS - 1];
			_root = *pp_root; 
			
//			_root = root->ptr[MAX_PTRS - 1];
//			std::cout<< "_root= " << (void *) _root << std::endl;
//			std::cout<< "lookup: target_key= " << target_key << std::endl;
			leaf = get_leaf(_root, target_key);
			p_num_keys = &leaf->num_keys[0];
			num_keys = *p_num_keys;
			for (int i = 0; i < num_keys; ++i) {
				p_key = &leaf->key[i];
				key = *p_key;
				if (target_key == key) {
					pp_val = &leaf->ptr[i];
					_pp_val = reinterpret_pointer_cast<char *>(pp_val);
					val = *_pp_val;
				//	std::cout<< "read from leaf node " << leaf->id << std::endl;
					//std::cout<< "read from leaf node " << leaf->id << " addr= " << (void *) leaf << std::endl;
				//	std::cout<< "pos=" << i << "\t" <<"key=" << target_key << "\t" <<"val=" << val << std::endl;
					return val;
				}
			}
			return NULL;
		}
};
























