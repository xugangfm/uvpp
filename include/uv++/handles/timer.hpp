//
// Created by Aaron on 8/1/2016.
//

#ifndef UV_TIMER_HANDLE_HPP
#define UV_TIMER_HANDLE_HPP

#include "base.hpp"

namespace uv {
    class Timer final : public Handle<uv_timer_t, Timer> {
        public:
            typedef typename Handle<uv_timer_t, Timer>::handle_t handle_t;

        protected:
            inline void _init() {
                uv_timer_init( this->_uv_loop, &_handle );
            }

            inline void _stop() {
                uv_timer_stop( &_handle );
            }

        public:
            template <typename Functor,
                      typename _Rep, typename _Period,
                      typename _Rep2 = uint64_t, typename _Period2 = std::milli>
            inline void start( Functor f,
                               const std::chrono::duration<_Rep, _Period> &timeout,
                               const std::chrono::duration<_Rep2, _Period2> &repeat =
                               std::chrono::duration<_Rep2, _Period2>(
                                   std::chrono::duration_values<_Rep2>::zero())) {

                typedef std::chrono::duration<uint64_t, std::milli> millis;

                typedef detail::Continuation<Functor, Timer> Cont;

                this->internal_data.continuation = std::make_shared<Cont>( f );

                uv_timer_start( this->handle(), []( uv_timer_t *h ) {
                                    HandleData *d = static_cast<HandleData *>(h->data);

                                    Timer *self = static_cast<Timer *>(d->self);

                                    static_cast<Cont *>(d->continuation.get())->dispatch( self );
                                },
                    //libuv expects milliseconds, so convert any duration given to milliseconds
                                std::chrono::duration_cast<millis>( timeout ).count(),
                                std::chrono::duration_cast<millis>( repeat ).count()
                );
            }
    };
}

#endif //UV_TIMER_HANDLE_HPP
