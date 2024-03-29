/*
 * boundedbuffer.hpp
 *
 *  Created on: 2 сент. 2021 г.
 *      Author: gleb
 */

#ifndef BOUNDEDBUFFER_H_
#define BOUNDEDBUFFER_H_

#include <boost/circular_buffer.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/thread.hpp>
#include <boost/call_traits.hpp>
#include <boost/progress.hpp>
#include <boost/bind.hpp>

template <class T>
class ring_buffer {
    public:
        typedef boost::circular_buffer<T> container_type;
        typedef typename container_type::size_type size_type;
        typedef typename container_type::value_type value_type;
        typedef typename boost::call_traits<value_type>::param_type param_type;

        explicit ring_buffer(size_type capacity) : m_unread(0), m_container(capacity) {}

        void push_front(param_type item) {
        // param_type represents the "best" way to pass a parameter of type value_type to a method
            boost::mutex::scoped_lock lock(m_mutex);
            m_not_full.wait(lock, boost::bind(&ring_buffer<value_type>::is_not_full, this));
            m_container.push_front(item);
            ++m_unread;
            lock.unlock();
            m_not_empty.notify_one();
        }

        void pop_back(value_type* pItem, std::atomic<bool> & is_work) {
            boost::mutex::scoped_lock lock(m_mutex);
            while(is_work.load()){
                if (m_not_empty.wait_for(lock, boost::chrono::seconds(1), boost::bind(&ring_buffer<value_type>::is_not_empty, this))) {
                    *pItem = m_container[--m_unread];
                    lock.unlock();
                    m_not_full.notify_one();
                    return;
                }
            }
        }

        unsigned int cur_count() { boost::mutex::scoped_lock lock(m_mutex); return (int)m_unread; }

    private:
        ring_buffer(const ring_buffer&);              // Disabled copy constructor
        ring_buffer& operator = (const ring_buffer&); // Disabled assign operator

        bool is_not_full() const { return m_unread < m_container.capacity(); }
        bool is_not_empty() const { return m_unread > 0; }

        size_type m_unread;
        container_type m_container;
        boost::mutex m_mutex;
        boost::condition m_not_empty;
        boost::condition m_not_full;
};

#endif /* BOUNDEDBUFFER_H_ */
