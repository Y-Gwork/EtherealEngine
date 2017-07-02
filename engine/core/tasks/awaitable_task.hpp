#ifndef AWAITABLE_TASK_HPP
#define AWAITABLE_TASK_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <forward_list>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "../common/nonstd/function_traits.hpp"
#include "../common/nonstd/sequence.hpp"
#include "../common/nonstd/type_traits.hpp"


namespace tasks
{
    /*
     * awaitable_task; a type-erased, allocator-aware std::packaged_task that
     * also contains its own arguments. The underlying packaged_task and the
     * stored argument tuple can be heap allocated or allocated with a provided
     * allocator.
     *
     * There are two forms of awaitable_tasks: ready tasks and async tasks.
     *
     *      Ready tasks are assumed to be immediately invokable; that is,
     *      invoking the underlying pakcaged_task with the provided arguments
     *      will not block. This is contrasted with async tasks where some or
     *      all of the provided arguments may be futures waiting on results of
     *      other tasks.
     *
     *      Awaitable tasks are assumed to take arguments where some or all are
     *      backed by futures waiting on results of other tasks. This is
     *      contrasted with ready tasks that are assumed to be immediately
     *      invokable.
     *
     * There are two helper methods for creating awaitable_task objects:
     * make_ready_task and make_awaitable_task, both of which return a pair of
     * the newly constructed awaitable_task and a std::future object to the
     * return value.
     */
    class awaitable_task
    {
        template <class T>
        using decay_if_future = typename std::conditional <
            nonstd::is_future <typename std::decay <T>::type>::value,
            typename nonstd::decay_future <T>::type, T
        >::type;

        struct ready_task_tag {};
        struct awaitable_task_tag {};

    public:
        awaitable_task () = default;
        ~awaitable_task () = default;

        awaitable_task (awaitable_task const &) = delete;
        awaitable_task (awaitable_task &&) noexcept = default;

        awaitable_task & operator= (awaitable_task const &) = delete;
        awaitable_task & operator= (awaitable_task &&) noexcept = default;

        void swap (awaitable_task & other) noexcept
        {
            std::swap (_t, other._t);
        }

        operator bool () const noexcept
        {
            return static_cast <bool> (_t);
        }

        friend class awaitable_task_system;

        template <class F, class ... Args>
        friend std::pair <
            awaitable_task,
            std::future <typename std::result_of <F (Args...)>::type>
        > make_ready_task (F && f, Args && ... args)
        {
            using pair_type = std::pair <
                awaitable_task,
                std::future <typename std::result_of <F (Args...)>::type>
            >;
            using model_type = ready_task_model <
                typename std::result_of <F (Args...)>::type (Args...)
            >;

            awaitable_task t (
                ready_task_tag (),
                std::forward <F> (f), std::forward <Args> (args)...
            );
            auto fut = static_cast <model_type &> (*t._t).get_future ();
            return pair_type (std::move (t), std::move (fut));
        }

        template <class Allocator, class F, class ... Args>
        friend std::pair <
            awaitable_task,
            std::future <typename std::result_of <F (Args...)>::type>
        > make_ready_task (std::allocator_arg_t, Allocator const & alloc,
                           F && f, Args && ... args)
        {
            using pair_type = std::pair <
                awaitable_task,
            std::future <typename std::result_of <F (Args...)>::type>
            >;
            using model_type = ready_task_model <
                typename std::result_of <F (Args...)>::type (Args...)
            >;

            awaitable_task t (
                ready_task_tag (), std::allocator_arg_t (), alloc,
                std::forward <F> (f), std::forward <Args> (args)...
            );
            auto fut = static_cast <model_type &> (*t._t).get_future ();
            return pair_type (std::move (t), std::move (fut));
        }

        template <class F, class ... Args>
        friend std::pair <
            awaitable_task,
            std::future <typename std::result_of <
                F (decay_if_future <Args>...)
            >::type>
        > make_awaitable_task (F && f, Args && ... args)
        {
            using pair_type = std::pair <
                awaitable_task,
                std::future <typename std::result_of <
                    F (decay_if_future <Args>...)
                >::type>
            >;
            using model_type = awaitable_task_model <
                typename std::result_of <
                    F (decay_if_future <Args>...)
                >::type (decay_if_future <Args>...), 
				Args...
            >;

            awaitable_task t (
                awaitable_task_tag (),
                std::forward <F> (f), std::forward <Args> (args)...
            );
            auto fut = static_cast <model_type &> (*t._t).get_future ();
            return pair_type (std::move (t), std::move (fut));
        }

        template <class Allocator, class F, class ... Args>
        friend std::pair <
            awaitable_task,
            std::future <typename std::result_of <
                F (decay_if_future <Args>...)
            >::type>
        > make_awaitable_task (std::allocator_arg_t, Allocator const & alloc,
                           F && f, Args && ... args)
        {
            using pair_type = std::pair <
                awaitable_task,
                std::future <typename std::result_of <
                    F (decay_if_future <Args>...)
                >::type>
            >;
            using model_type = awaitable_task_model <
                typename std::result_of <
                    F (decay_if_future <Args>...)
                >::type (decay_if_future <Args>...),
				Args...
            >;

            awaitable_task t (
                awaitable_task_tag (), std::allocator_arg_t (), alloc,
                std::forward <F> (f), std::forward <Args> (args)...
            );
            auto fut = static_cast <model_type &> (*t._t).get_future ();
            return pair_type (std::move (t), std::move (fut));
        }

        void operator() ()
        {
            if (_t)
                _t->invoke_ ();
            else
                throw std::logic_error ("bad task access");
        }

        bool ready () const
        {
            if (_t)
                return _t->ready_ ();
            else
                throw std::logic_error ("bad task access");
        }

    private:
        template <class F, class ... Args>
        awaitable_task (ready_task_tag, F && f, Args && ... args)
            : _t (new ready_task_model <
                    typename std::result_of <F (Args...)>::type (Args...)
                > (std::forward <F> (f), std::forward <Args> (args)...)
            )
        {}

        template <class Allocator, class F, class ... Args>
        awaitable_task (ready_task_tag,
              std::allocator_arg_t,
              Allocator const & alloc,
              F && f, Args && ... args)
            : _t (
                new ready_task_model <
                    typename std::result_of <F (Args...)>::type (Args...)
                > (std::allocator_arg_t (), alloc,
                   std::forward <F> (f), std::forward <Args> (args)...)
            )
        {
        }

        template <class F, class ... Args>
        awaitable_task (awaitable_task_tag, F && f, Args && ... args)
            : _t (
                new awaitable_task_model <
                    typename std::result_of <
                        F (decay_if_future <Args>...)
                    >::type (decay_if_future <Args>...),
                    Args...
                > (std::forward <F> (f), std::forward <Args> (args)...)
            )
        {}

        template <class Allocator, class F, class ... Args>
        awaitable_task (awaitable_task_tag,
              std::allocator_arg_t,
              Allocator const & alloc,
              F && f, Args && ... args)
            : _t (
                new awaitable_task_model <
                    typename std::result_of <
                        F (decay_if_future <Args>...)
                    >::type (decay_if_future <Args>...),
                    Args...
                > (std::allocator_arg_t (), alloc,
                   std::forward <F> (f), std::forward <Args> (args)...)
            )
        {}

        struct task_concept
        {
            virtual ~task_concept () noexcept {}
            virtual void invoke_ () = 0;
            virtual bool ready_ () const noexcept = 0;
        };

        template <class> struct ready_task_model;

        /*
         * Ready tasks are assumed to be immediately invokable; that is,
         * invoking the underlying pakcaged_task with the provided arguments
         * will not block. This is contrasted with async tasks where some or all
         * of the provided arguments may be futures waiting on results of other
         * tasks.
         */
        template <class R, class ... Args>
        struct ready_task_model <R (Args...)> : task_concept
        {
            template <class F>
            explicit ready_task_model (F && f, Args && ... args)
                : _f    (std::forward <F> (f))
                , _args (std::forward <Args> (args)...)
            {}

            template <class Allocator, class F>
            explicit ready_task_model (
                std::allocator_arg_t, Allocator const & alloc,
                F && f, Args && ... args
            )
                : _f    (std::allocator_arg_t (), alloc, std::forward <F> (f))
                , _args (std::allocator_arg_t (), alloc,
                         std::forward <Args> (args)...)
            {}

            std::future <R> get_future ()
            {
                return _f.get_future ();
            }

            void invoke_ () override
            {
                nonstd::apply (_f, _args);
            }

            bool ready_ () const noexcept override
            {
                return true;
            }

        private:
            std::packaged_task <R (Args...)> _f;
            std::tuple <Args...> _args;
        };

        template <class ...> struct awaitable_task_model;

        /*
         * Async tasks are assumed to take arguments where some or all are
         * backed by futures waiting on results of other tasks. This is
         * contrasted with ready tasks that are assumed to be immediately
         * invokable.
         */
        template <class R, class ... CallArgs, class ... FutArgs>
        struct awaitable_task_model <R (CallArgs...), FutArgs...> : task_concept
        {
            template <class F, class ... Args>
            explicit awaitable_task_model (F && f, Args && ... args)
                : _f    (std::forward <F> (f))
                , _args (std::forward <Args> (args)...)
            {}

            template <class Allocator, class F, class ... Args>
            explicit awaitable_task_model (
                std::allocator_arg_t, Allocator const & alloc,
                F && f, Args && ... args
            )
                : _f    (std::allocator_arg_t (), alloc, std::forward <F> (f))
                , _args (std::allocator_arg_t (), alloc,
                         std::forward <Args> (args)...)
            {}

            std::future <R> get_future ()
            {
                return _f.get_future ();
            }

            void invoke_ () override 
			{ 
				constexpr const std::size_t arity = sizeof...(FutArgs);
				do_invoke_ (nonstd::make_index_sequence<arity>());
			}

            bool ready_ () const noexcept override
            {
				constexpr const std::size_t arity = sizeof...(FutArgs);
                return do_ready_ (nonstd::make_index_sequence<arity>());
            }

        private:
            template <class T>
            static inline auto call_get (T && t) noexcept
                -> decltype (std::forward <T> (t))
            {
                return std::forward <T> (t);
            }

            template <class T>
            static inline auto call_get (std::future <T> && t) noexcept
                -> decltype (t.get())
            {
                return t.get();
            }

            template <std::size_t ... I>
            inline void do_invoke_ (nonstd::index_sequence <I...>)
            {
				_f(call_get(std::get <I>(std::move(_args)))...);
            }

            template <class T>
            static inline bool call_ready (T &) noexcept { return true; }

            template <class T>
            static inline bool call_ready (std::future <T> & t) noexcept
            {
				using namespace std::chrono_literals;
                return t.wait_for (0s) == std::future_status::ready;
            }

            template <std::size_t ... I>
            inline bool do_ready_ (nonstd::index_sequence <I...>) const noexcept
            {
				bool all_true = nonstd::check_all_true(call_ready(std::get<I>(_args))...);
				return all_true;
			}

            std::packaged_task <R (CallArgs...)> _f;
            std::tuple <FutArgs...> _args;
        };

        std::unique_ptr <task_concept> _t;
    };

    
    class awaitable_task_system
    {
		using Allocator = std::allocator <awaitable_task>;
        class task_queue
        {
            using iterator_type =
                typename std::forward_list <awaitable_task>::iterator;

            std::forward_list <awaitable_task> tasks_;
            iterator_type last_;
            std::condition_variable cv_;
            std::mutex mutex_;
            std::atomic_bool done_ {false};

            /* rotates the first element of the list onto the end */
            void rotate_with_ (std::forward_list <awaitable_task> & side_buf)
            {
                /* zero or one element list -- trivial to rotate */
                if (tasks_.empty () || last_ == tasks_.begin ())
                    return;

                side_buf.splice_after (side_buf.begin (), tasks_);
                tasks_.splice_after (
                    tasks_.begin (), side_buf,
                    side_buf.begin (), side_buf.end ()
                );

                auto new_last = side_buf.begin ();
                tasks_.splice_after (last_, side_buf);
                last_ = new_last;
            }

        public:
            task_queue ()
                : tasks_ {}
            {}

            task_queue (task_queue const &) = delete;

            task_queue (task_queue && other) noexcept
                : tasks_ (std::move (other).tasks_)
                , last_  (std::move (other).last_)
                , done_  (other.done_.load ())
            {}

            void set_done ()
            {
                done_.store (true);
                cv_.notify_all ();
            }

            std::pair <bool, awaitable_task> try_pop ()
            {
                std::unique_lock <std::mutex>lock (mutex_, std::try_to_lock);

                if (!lock || tasks_.empty ())
				{
                    return std::make_pair (false, awaitable_task {});
                } 
				else 
				{
                    auto t = std::move (tasks_.front ());
                    tasks_.pop_front ();
                    return std::make_pair (true, std::move (t));
                }
            }

            bool try_push (awaitable_task & t)
            {
                {
                    std::unique_lock <std::mutex> lock(mutex_, std::try_to_lock);
                    if (!lock)
                        return false;

                    if (tasks_.empty ())
					{
                        tasks_.emplace_front (std::move (t));
                        last_ = tasks_.begin ();
                    } 
					else 
					{
                        last_ = tasks_.emplace_after (last_, std::move (t));
                    }
                }

                cv_.notify_one ();
                return true;
            }

            std::pair <bool, awaitable_task> pop ()
            {
                std::unique_lock <std::mutex> lock (mutex_);
                while (tasks_.empty () && !done_)
                    cv_.wait (lock);

                if (tasks_.empty ())
                    return std::make_pair (false, awaitable_task {});

                auto iter = tasks_.begin ();
                auto const old_last = last_;
                std::forward_list <awaitable_task> side_buf;

                if (iter->ready ())
				{
                    auto t = std::move (*iter);
                    tasks_.pop_front ();
                    return std::make_pair (true, std::move (t));
                } 
				else 
				{
                    rotate_with_ (side_buf);
                }

                for (auto lag = iter++; lag != old_last; lag = iter++) 
				{
                    if (iter->ready ()) 
					{
                        if (last_ == iter)
                            last_ = lag;
                        auto t = std::move (*iter);
                        tasks_.erase_after (lag);
                        return std::make_pair (true, std::move (t));
                    }
					else
					{
                        rotate_with_ (side_buf);
                    }
                }

                /*
                 * If we get to this point the best we can do is pop from the
                 * front of the task list, release the lock, and wait for the
                 * task to be ready.
                 */
                auto t = std::move (tasks_.front ());
                tasks_.pop_front ();
                lock.unlock ();

                while (!t.ready ())
                    std::this_thread::yield ();

                return std::make_pair (true, std::move (t));
            }

            void push (awaitable_task t)
            {
                {
                    std::unique_lock <std::mutex> lock (mutex_);
					if (tasks_.empty ())
					{
                        tasks_.emplace_front (std::move (t));
                        last_ = tasks_.begin ();
                    } 
					else 
					{
                        last_ = tasks_.emplace_after (last_, std::move (t));
                    }
                }
                cv_.notify_one ();
            }
        };

        std::vector <task_queue> queues_;
        std::vector <std::thread> threads_;
        typename Allocator::template rebind <awaitable_task::task_concept>::other
            alloc_;
        std::size_t nthreads_;
        std::size_t current_index_ {1};

        void run (std::size_t idx)
        {
            while (true) 
			{
				std::pair <bool, awaitable_task> p = { false, awaitable_task() };


                for (std::size_t k = 0; k < 10 * nthreads_; ++k)
				{
					const auto queue_index = get_thread_queue_idx(idx, k);
                    p = queues_ [queue_index].try_pop ();
                    if (p.first)
                        break;
                }

                if (!p.first)
				{
                    p = queues_ [idx].pop ();
                    if (!p.first)
                        return;
                }

                p.second ();
            }
        }

		size_t get_thread_queue_idx(size_t idx, size_t seed = 0)
		{
			return ((idx + seed) % nthreads_) + 1;
		}

		size_t get_main_thread_queue_idx()
		{
			return 0;
		}

    public:
        awaitable_task_system ()
			: awaitable_task_system(std::thread::hardware_concurrency() - 1)
        {}

        awaitable_task_system (std::size_t nthreads,
                           Allocator const & alloc = Allocator ())
            : queues_   {}
            , threads_  {}
            , alloc_    (alloc)
            , nthreads_ {nthreads}
        {
			// +1 for the main thread's queue
            queues_.reserve (nthreads + 1);
			queues_.emplace_back();
            for (std::size_t th = 1; th < nthreads + 1; ++th)
                queues_.emplace_back ();

            threads_.reserve (nthreads);
            for (std::size_t th = 1; th < nthreads + 1; ++th)
                threads_.emplace_back (
                    &awaitable_task_system::run, this, th
                );
        }

		//-----------------------------------------------------------------------------
		//  Name : ~awaitable_task_system ()
		/// <summary>
		/// Notifies threads to finish and joins them.
		/// </summary>
		//-----------------------------------------------------------------------------
        ~awaitable_task_system ()
        {
            done ();
			for (auto & th : threads_)
			{
				if(th.joinable())
					th.join ();
			}
        }

		//-----------------------------------------------------------------------------
		//  Name : done ()
		/// <summary>
		/// Notify queues on threads to finish.
		/// </summary>
		//-----------------------------------------------------------------------------
        void done () noexcept
        {
            for (auto & q : queues_)
                q.set_done ();
        }


		//-----------------------------------------------------------------------------
		//  Name : push_ready ()
		/// <summary>
		/// Pushes a immediately invokable task to be executed.
		/// Ready tasks are assumed to be immediately invokable; that is,
		/// invoking the underlying pakcaged_task with the provided arguments
		/// will not block.This is contrasted with async tasks where some or
		/// all of the provided arguments may be futures waiting on results of
		/// other tasks.
		/// </summary>
		//-----------------------------------------------------------------------------
        template <class F, class ... Args>
        auto push_ready (F && f, Args && ... args)
            -> typename std::remove_reference <
                decltype (make_ready_task (
                    std::allocator_arg_t {}, alloc_,
                    std::forward <F> (f), std::forward <Args> (args)...
                ).second)
            >::type
        {
			if (nthreads_ == 0)
			{
				return push_ready_on_main(std::forward <F>(f), std::forward <Args>(args)...);
			}
			else
			{
				auto t = make_ready_task(
					std::allocator_arg_t{}, alloc_,
					std::forward <F>(f), std::forward <Args>(args)...
				);

				auto const idx = current_index_++;
				for (std::size_t k = 0; k < 10 * nthreads_; ++k)
				{
					const auto queue_index = get_thread_queue_idx(idx, k);
					if (queues_[queue_index].try_push(t.first))
						return std::move(t.second);
				}

				const auto queue_index = get_thread_queue_idx(idx);
				queues_[queue_index].push(std::move(t.first));
				return std::move(t.second);
			}
        }

		//-----------------------------------------------------------------------------
		//  Name : push_awaitable ()
		/// <summary>
		/// Pushes an awaitable task to be executed.
		/// Awaitable tasks are assumed to take arguments where some or all are
		/// backed by futures waiting on results of other tasks.This is
		/// contrasted with ready tasks that are assumed to be immediately invokable.
		/// </summary>
		//-----------------------------------------------------------------------------
        template <class F, class ... Args>
        auto push_awaitable (F && f, Args && ... args)
            -> typename std::remove_reference <
                decltype (make_awaitable_task (
                    std::allocator_arg_t {}, alloc_,
                    std::forward <F> (f), std::forward <Args> (args)...
                ).second)
            >::type
        {
			if (nthreads_ == 0)
			{
				return push_awaitable_on_main(std::forward <F>(f), std::forward <Args>(args)...);
			}
			else
			{
				auto t = make_awaitable_task(
					std::allocator_arg_t{}, alloc_,
					std::forward <F>(f), std::forward <Args>(args)...
				);

				auto const idx = current_index_++;
				for (std::size_t k = 0; k < 10 * nthreads_; ++k)
				{
					const auto queue_index = get_thread_queue_idx(idx, k);
					if (queues_[queue_index].try_push(t.first))
						return std::move(t.second);
				}

				const auto queue_index = get_thread_queue_idx(idx);
				queues_[queue_index].push(std::move(t.first));
				return std::move(t.second);
			}
            
        }

		//-----------------------------------------------------------------------------
		//  Name : push_awaitable ()
		/// <summary>
		/// Pushes an awaitable task to be executed.
		/// Awaitable tasks are assumed to take arguments where some or all are
		/// backed by futures waiting on results of other tasks.This is
		/// contrasted with ready tasks that are assumed to be immediately invokable.
		/// </summary>
		//-----------------------------------------------------------------------------
        void push_awaitable (awaitable_task && t)
        {
			if (nthreads_ == 0)
			{
				push_awaitable_on_main(std::forward<awaitable_task>(t));
			}
			else
			{
				auto const idx = current_index_++;
				for (std::size_t k = 0; k < 10 * nthreads_; ++k)
				{
					const auto queue_index = get_thread_queue_idx(idx, k);
					if (queues_[queue_index].try_push(t))
						return;
				}

				const auto queue_index = get_thread_queue_idx(idx);
				queues_[queue_index].push(std::move(t));
			}
        }


		//-----------------------------------------------------------------------------
		//  Name : push_ready_on_main ()
		/// <summary>
		/// Pushes a immediately invokable task to be executed.
		/// Ready tasks are assumed to be immediately invokable; that is,
		/// invoking the underlying pakcaged_task with the provided arguments
		/// will not block.This is contrasted with async tasks where some or
		/// all of the provided arguments may be futures waiting on results of
		/// other tasks.
		/// </summary>
		//-----------------------------------------------------------------------------
		template <class F, class ... Args>
		auto push_ready_on_main(F && f, Args && ... args)
			-> typename std::remove_reference <
			decltype (make_ready_task(
				std::allocator_arg_t{}, alloc_,
				std::forward <F>(f), std::forward <Args>(args)...
			).second)
			>::type
		{
			auto t = make_ready_task(
				std::allocator_arg_t{}, alloc_,
				std::forward <F>(f), std::forward <Args>(args)...
			);
			const auto queue_index = get_main_thread_queue_idx();
			for (std::size_t k = 0; k < 10 * nthreads_; ++k)
			{
				if (queues_[queue_index].try_push(t.first))
					return std::move(t.second);
			}

			queues_[queue_index].push(std::move(t.first));
			return std::move(t.second);
		}


		//-----------------------------------------------------------------------------
		//  Name : push_awaitable_on_main ()
		/// <summary>
		/// Pushes an awaitable task to be executed.
		/// Awaitable tasks are assumed to take arguments where some or all are
		/// backed by futures waiting on results of other tasks.This is
		/// contrasted with ready tasks that are assumed to be immediately invokable.
		/// </summary>
		//-----------------------------------------------------------------------------
		template <class F, class ... Args>
		auto push_awaitable_on_main(F && f, Args && ... args)
			-> typename std::remove_reference <
			decltype (make_awaitable_task(
				std::allocator_arg_t{}, alloc_,
				std::forward <F>(f), std::forward <Args>(args)...
			).second)
			>::type
		{
			auto t = make_awaitable_task(
				std::allocator_arg_t{}, alloc_,
				std::forward <F>(f), std::forward <Args>(args)...
			);

			const auto queue_index = get_main_thread_queue_idx();
			for (std::size_t k = 0; k < 10; ++k)
			{
				if (queues_[queue_index].try_push(std::move(t.first)))
					return std::move(t.second);
			}

			queues_[queue_index].push(std::move(t.first));
			return std::move(t.second);
		}

		//-----------------------------------------------------------------------------
		//  Name : push_awaitable_on_main ()
		/// <summary>
		/// Pushes an awaitable task to be executed.
		/// Awaitable tasks are assumed to take arguments where some or all are
		/// backed by futures waiting on results of other tasks.This is
		/// contrasted with ready tasks that are assumed to be immediately invokable.
		/// </summary>
		//-----------------------------------------------------------------------------
		void push_awaitable_on_main(awaitable_task && t)
		{
			const auto queue_index = get_main_thread_queue_idx();
			for (std::size_t k = 0; k < 10; ++k)
			{
				if (queues_[queue_index].try_push(t))
					return;
			}

			queues_[queue_index].push(std::move(t));
		}

		//-----------------------------------------------------------------------------
		//  Name : run_on_main ()
		/// <summary>
		/// Process main thread tasks
		/// </summary>
		//-----------------------------------------------------------------------------
		void run_on_main()
		{	
			std::pair <bool, awaitable_task> p = { false, awaitable_task() };

			const auto queue_index = get_main_thread_queue_idx();
			for (std::size_t k = 0; k < 10; ++k)
			{
				p = queues_[queue_index].try_pop();
				if (p.first)
					break;
			}

			if (!p.first)
			{
				p = queues_[queue_index].pop();
				if (!p.first)
					return;
			}

			p.second();
			
		}
    };
}   // namespace tasks

#endif  // #ifndef AWAITABLE_TASK_HPP
