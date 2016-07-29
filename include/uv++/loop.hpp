//
// Created by Aaron on 7/24/2016.
//

#ifndef UV_LOOP_HPP
#define UV_LOOP_HPP

#include "fwd.hpp"

#include "exception.hpp"
#include "type_traits.hpp"

#include "handle.hpp"
#include "async.hpp"
#include "fs.hpp"

#include <thread>
#include <unordered_set>
#include <iomanip>

#ifndef UV_DEFAULT_LOOP_SLEEP
#define UV_DEFAULT_LOOP_SLEEP 1ms
#endif

namespace uv {
    class Loop final : public HandleBase<uv_loop_t> {
        public:
            typedef typename HandleBase<uv_loop_t>::handle_t handle_t;

            template <typename H, typename D>
            friend
            class Handle;

            friend class Filesystem;

        private:
            bool external;

            Filesystem _fs;

            std::atomic_bool stopped;

            typedef std::unordered_set<std::shared_ptr<void>>             handle_set_type;
            handle_set_type                                               handle_set;

            typedef std::pair<uv_handle_t *, void ( * )( uv_handle_t * )> close_args;

            std::shared_ptr<Async<close_args>> close_async;

        protected:
            inline void _init() {
                if( !this->external ) {
                    uv_loop_init( this->handle());
                }

                //The reference on auto& is essential
                this->close_async = this->async<close_args>( [this]( auto &, close_args h ) {
                    uv_close( h.first, h.second );
                } );
            }

        public:
            enum class uv_option : std::underlying_type<uv_loop_option>::type {
                    BLOCK_SIGNAL = UV_LOOP_BLOCK_SIGNAL
            };

            enum class uvp_option {
                    WAIT_ON_CLOSE
            };

            inline const handle_t *handle() const {
                return _loop;
            }

            inline handle_t *handle() {
                return _loop;
            }

            inline Loop()
                : _fs( this ), external( false ) {
                this->init( this, new handle_t );
            }

            explicit inline Loop( handle_t *l )
                : _fs( this ), external( true ) {
                this->init( this, l );
            }

            inline Filesystem *fs() {
                return &_fs;
            }

            inline int run( uv_run_mode mode = UV_RUN_DEFAULT ) {
                stopped = false;

                return uv_run( handle(), mode );
            }

            template <typename _Rep, typename _Period>
            inline void run_forever( const std::chrono::duration<_Rep, _Period> &delay ) {
                stopped = false;

                while( !stopped ) {
                    if( !this->run()) {
                        std::this_thread::sleep_for( delay );
                    }
                }
            }

            inline void run_forever() {
                using namespace std::chrono_literals;

                this->run_forever( UV_DEFAULT_LOOP_SLEEP );
            }

            inline void start() {
                this->run_forever();
            }

            inline void stop() {
                stopped = true;

                uv_stop( handle());
            }

            template <typename... Args>
            typename std::enable_if<all_type<uv_loop_option, Args...>::value, Loop &>::type
            inline configure( Args &&... args ) {
                auto res = uv_loop_configure( handle(), std::forward<Args>( args )... );

                if( res != 0 ) {
                    throw Exception( res );
                }

                return *this;
            }

            inline static size_t size() {
                return uv_loop_size();
            }

            inline int backend_fs() const {
                return uv_backend_fd( handle());
            }

            inline int backend_timeout() const {
                return uv_backend_timeout( handle());
            }

            inline uint64_t now() const {
                return uv_now( handle());
            }

            inline void update_time() {
                uv_update_time( handle());
            }

            //returns true on closed
            inline bool try_close( int *resptr = nullptr ) {
                int res = uv_loop_close( handle());

                if( resptr != nullptr ) {
                    *resptr = res;
                }

                return ( res == 0 );
            }

            //returns true on closed, throws otherwise
            inline bool close() {
                int res;

                if( !this->try_close( &res )) {
                    throw Exception( res );
                }

                return true;
            }

            ~Loop() {
                for( std::shared_ptr<void> x : handle_set ) {
                    static_cast<HandleBase<uv_handle_t> *>(x.get())->stop();
                }

                if( !this->external ) {
                    this->stop();

                    delete handle();
                }
            }

        protected:
            template <typename H, typename... Args>
            inline std::shared_ptr<H> new_handle( Args &&... args ) {
                std::shared_ptr<H> p = std::make_shared<H>();

                auto it_inserted = handle_set.insert( p );

                //On the really.... REALLY off chance there is a collision for a new pointer, just use the old one
                if( !it_inserted.second ) {
                    p = std::static_pointer_cast<H>( *it_inserted.first );
                }

                p->init( this );

                p->start( std::forward<Args>( args )... );

                return p;
            }

        public:
            template <typename... Args>
            inline std::shared_ptr<Idle> idle( Args &&... args ) {
                return new_handle<Idle>( std::forward<Args>( args )... );
            }

            template <typename... Args>
            inline std::shared_ptr<Prepare> prepare( Args &&... args ) {
                return new_handle<Prepare>( std::forward<Args>( args )... );
            }

            template <typename... Args>
            inline std::shared_ptr<Check> check( Args &&... args ) {
                return new_handle<Check>( std::forward<Args>( args )... );
            }

            template <typename Functor,
                      typename _Rep, typename _Period,
                      typename _Rep2 = uint64_t, typename _Period2 = std::milli>
            inline std::shared_ptr<Timer> timer( Functor f,
                                                 const std::chrono::duration<_Rep, _Period> &timeout,
                                                 const std::chrono::duration<_Rep2, _Period2> &repeat =
                                                 std::chrono::duration<_Rep2, _Period2>(
                                                     std::chrono::duration_values<_Rep2>::zero())) {
                return new_handle<Timer>( f, timeout, repeat );
            }

            template <typename Functor,
                      typename _Rep, typename _Period,
                      typename _Rep2, typename _Period2>
            inline std::shared_ptr<Timer> timer( const std::chrono::duration<_Rep, _Period> &timeout,
                                                 const std::chrono::duration<_Rep2, _Period2> &repeat,
                                                 Functor f ) {
                return this->timer( f, timeout, repeat );
            }

            template <typename Functor,
                      typename _Rep, typename _Period,
                      typename _Rep2 = uint64_t, typename _Period2 = std::milli>
            inline std::shared_ptr<Timer> timer( const std::chrono::duration<_Rep, _Period> &timeout,
                                                 Functor f,
                                                 const std::chrono::duration<_Rep2, _Period2> &repeat =
                                                 std::chrono::duration<_Rep2, _Period2>(
                                                     std::chrono::duration_values<_Rep2>::zero())) {
                return this->timer( f, timeout, repeat );
            }

            template <typename Functor,
                      typename _Rep, typename _Period,
                      typename _Rep2 = uint64_t, typename _Period2 = std::milli>
            inline std::shared_ptr<Timer> repeat( const std::chrono::duration<_Rep, _Period> &repeat,
                                                  Functor f,
                                                  const std::chrono::duration<_Rep2, _Period2> &timeout =
                                                  std::chrono::duration<_Rep2, _Period2>(
                                                      std::chrono::duration_values<_Rep2>::zero())) {
                return this->timer( f, timeout, repeat );
            }

            template <typename Functor,
                      typename _Rep, typename _Period,
                      typename _Rep2 = uint64_t, typename _Period2 = std::milli>
            inline std::shared_ptr<Timer> repeat( Functor f,
                                                  const std::chrono::duration<_Rep, _Period> &repeat,
                                                  const std::chrono::duration<_Rep2, _Period2> &timeout =
                                                  std::chrono::duration<_Rep2, _Period2>(
                                                      std::chrono::duration_values<_Rep2>::zero())) {
                return new_handle<Timer>( f, timeout, repeat );
            }

            template <typename P = void, typename R = void, typename... Args>
            inline std::shared_ptr<Async<P, R>> async( Args &&... args ) {
                return new_handle<Async<P, R>>( std::forward<Args>( args )... );
            };

            template <typename... Args>
            inline std::shared_ptr<Signal> signal( Args &&... args ) {
                return new_handle<Signal>( std::forward<Args>( args )... );
            }

            /*
             * This is such a mess, but that's what I get for mixing C and C++
             *
             * Most of it is partially copied from src/uv-common.(h|c) and src/queue.h
             * */
            template <typename _Char = char>
            void print_handles( std::basic_ostream<_Char> &out, bool only_active = false ) const {
                typedef void *UV_QUEUE[2];
#ifndef _WIN32
                enum {
                  UV__HANDLE_INTERNAL = 0x8000,
                  UV__HANDLE_ACTIVE   = 0x4000,
                  UV__HANDLE_REF      = 0x2000,
                  UV__HANDLE_CLOSING  = 0 /* no-op on unix */
                };
#else
#define UV__HANDLE_INTERNAL  0x80
#define UV__HANDLE_ACTIVE    0x40
#define UV__HANDLE_REF       0x20
#define UV__HANDLE_CLOSING   0x01
#endif

#define UV_QUEUE_NEXT( q )                  (*(UV_QUEUE **) &((*(q))[0]))
#define UV_QUEUE_DATA( ptr, type, field )   ((type *) ((char *) (ptr) - offsetof(type, field)))
#define UV_QUEUE_FOREACH( q, h )            for ((q) = UV_QUEUE_NEXT(h); (q) != (h); (q) = UV_QUEUE_NEXT(q))

                const char        *type;
                UV_QUEUE          *q;
                uv_handle_t const *h;

                UV_QUEUE_FOREACH( q, &handle()->handle_queue ) {
                    h = UV_QUEUE_DATA( q, uv_handle_t, handle_queue );

                    if( !only_active || ( h->flags & UV__HANDLE_ACTIVE != 0 )) {

                        switch( h->type ) {
#define X( uc, lc ) case UV_##uc: type = #lc; break;
                            UV_HANDLE_TYPE_MAP( X )
#undef X
                            default:
                                type = "<unknown>";
                        }

                        out << "["
                            << "R-"[!( h->flags & UV__HANDLE_REF )]
                            << "A-"[!( h->flags & UV__HANDLE_ACTIVE )]
                            << "I-"[!( h->flags & UV__HANDLE_INTERNAL )]
                            << "] " << std::setw( 8 ) << std::left << type
                            << " 0x" << h
                            << std::endl;
                    }
                }

#undef UV_QUEUE_NEXT
#undef UV_QUEUE_DATA
#undef UV_QEUEU_FOREACH

#ifdef _WIN32
#undef UV__HANDLE_INTERNAL
#undef UV__HANDLE_ACTIVE
#undef UV__HANDLE_REF
#undef UV__HANDLE_CLOSING
#endif
            }
    };

    namespace detail {
        static std::shared_ptr<::uv::Loop> default_loop_ptr;
    }

    std::shared_ptr<Loop> default_loop() {
        using namespace ::uv::detail;

        if( !default_loop_ptr ) {
            default_loop_ptr = std::make_shared<Loop>( uv_default_loop());
        }

        return default_loop_ptr;
    }

    template <typename H>
    inline void HandleBase<H>::init( Loop *l ) {
        assert( l != nullptr );

        this->init( l, l->handle());
    }

    template <typename H, typename D>
    template <typename Functor>
    inline std::shared_future<void> Handle<H, D>::close( Functor f ) {
        typedef detail::Continuation<Functor> Cont;

        this->internal_data.secondary_continuation = std::make_shared<Cont>( f );

        return this->loop()->close_async->send( std::make_pair((uv_handle_t *)( this->handle()), []( uv_handle_t *h ) {
            HandleData *d = static_cast<HandleData *>(h->data);

            typename Handle<H, D>::derived_type *self = static_cast<typename Handle<H, D>::derived_type *>(d->self);

            static_cast<Cont *>(d->secondary_continuation.get())->f( *self );
        } ));
    }

    void Filesystem::init( Loop *l ) {
        assert( l != nullptr );

        this->_loop = l->handle();
    }
}

#ifdef UV_OVERLOAD_OSTREAM

template <typename _Char>
std::basic_ostream<_Char> &operator<<( std::basic_ostream<_Char> &out, const uv::Loop &loop ) {
    loop.print_handles<_Char>( out, false );

    return out;
}

#endif

#endif //UV_LOOP_HPP
