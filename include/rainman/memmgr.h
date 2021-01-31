#ifndef RAINMAN_MEMMGR_H
#define RAINMAN_MEMMGR_H

#include <cstdint>
#include <cstring>
#include <semaphore.h>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <typeinfo>
#include "errors.h"
#include "memmap.h"

namespace rainman {
    class memmgr {
    private:
        uint64_t allocation_size{};
        uint64_t n_allocations{};
        uint64_t peak_size{};
        memmap *memmap{};
        memmgr *parent{};
        std::unordered_map<memmgr *, bool> children{};
        std::mutex mutex{};

        void lock();

        void unlock();

        void update(uint64_t alloc_size, uint64_t alloc_count);

    public:
        memmgr(uint64_t map_size = 0xffff);

        ~memmgr() {
            lock();
            delete memmap;
            unlock();
        }

        template<typename Type>
        Type *r_malloc(uint64_t n_elems) {
            lock();

            uint64_t curr_alloc_size = sizeof(Type) * n_elems;

            if (peak_size != 0 && allocation_size + curr_alloc_size > peak_size) {
                unlock();
                throw MemoryErrors::PeakLimitReachedException();
            }

            if (parent != nullptr &&
                parent->peak_size != 0 &&
                parent->get_alloc_size() + curr_alloc_size > parent->get_peak_size()) {
                unlock();
                throw MemoryErrors::PeakLimitReachedException();
            }

            auto elem = new map_elem;

            elem->ptr = new Type[n_elems];
            elem->alloc_size = n_elems * sizeof(Type);
            elem->count = n_elems;
            elem->type_name = typeid(Type).name();
            elem->next = nullptr;

            memmap->add(elem);

            update(allocation_size + elem->alloc_size, n_allocations + 1);

            unlock();

            return static_cast<Type*>(elem->ptr);
        }

        template<typename Type>
        void r_free(Type *ptr) {
            if (ptr == nullptr) {
                return;
            }

            lock();

            auto *elem = memmap->get((void *) ptr);
            if (elem != nullptr) {
                update(allocation_size - elem->alloc_size, n_allocations - 1);
                unlock();
                memmap->remove_by_type<Type>(ptr);
                lock();
            } else {
                unlock();
                for (auto child : children) {
                    child.first->r_free(ptr);
                }
                lock();
            }

            unlock();
        }

        template<typename Type, typename ...Args>
        Type *r_new(uint64_t n_elems, Args ...args) {
            lock();

            uint64_t curr_alloc_size = sizeof(Type) * n_elems;

            if (peak_size != 0 && allocation_size + curr_alloc_size > peak_size) {
                unlock();
                throw MemoryErrors::PeakLimitReachedException();
            }

            if (parent != nullptr &&
                parent->peak_size != 0 &&
                parent->get_alloc_size() + curr_alloc_size > parent->get_peak_size()) {
                unlock();
                throw MemoryErrors::PeakLimitReachedException();
            }

            auto elem = new map_elem;

            elem->alloc_size = n_elems * sizeof(Type);
            elem->count = n_elems;
            elem->ptr = operator new[](elem->alloc_size);
            elem->type_name = typeid(Type).name();
            elem->next = nullptr;
            elem->is_raw = true;

            memmap->add(elem);

            update(allocation_size + elem->alloc_size, n_allocations + 1);

            unlock();

            Type *objects = reinterpret_cast<Type*>(elem->ptr);

            for (uint64_t i = 0; i < n_elems; i++) {
                new(objects + i) Type(std::forward<Args>(args)...);
            }

            return objects;
        }

        void set_peak(uint64_t _peak_size);

        void set_parent(memmgr *p);

        memmgr *get_parent();

        void unregister();

        uint64_t get_alloc_count();

        uint64_t get_alloc_size();

        void print_mem_trace();

        uint64_t get_peak_size();

        memmgr *create_child_mgr();

        // De-allocate everything allocated by the memory manager by type.
        template<typename Type>
        void wipe(bool deep_wipe = false) {
            lock();

            auto *curr = memmap->head;
            while (curr != nullptr) {
                auto next = curr->next_iter;
                auto ptr = curr->ptr;
                if (ptr == nullptr) {
                    curr = next;
                    continue;
                }

                auto *elem = memmap->get((void *) ptr);
                if (elem != nullptr) {
                    if (strcmp(typeid(Type).name(), elem->type_name) == 0) {
                        update(allocation_size - elem->alloc_size, n_allocations - 1);
                        memmap->remove_by_type<Type *>(reinterpret_cast<Type *>(ptr));
                    }
                }

                curr = next;
            }

            unlock();

            if (deep_wipe) {
                for (auto &child : children) {
                    child.first->wipe<Type>();
                }
            }
        }
    };
}


#endif
