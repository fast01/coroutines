// (c) 2013 Maciej Gajewski, <maciej.gajewski0@gmail.com>
#include "coroutines/context.hpp"
#include "coroutines/scheduler.hpp"

//#define CORO_LOGGING
#include "coroutines/logging.hpp"

namespace coroutines {

static thread_local context* __current_context = nullptr;

context::context(scheduler* parent)
    : _parent(parent)
{
}

context* context::current_context()
{
    return __current_context;
}

void context::block(const std::string& checkpoint_name)
{
    assert(!_blocked);
    std::list<coroutine_weak_ptr> coros;
    _queue.get_all(coros);

    coroutine::current_corutine()->set_checkpoint(checkpoint_name);
    _blocked = true;
    _parent->context_blocked(this, coros, checkpoint_name);
}

void context::unblock(const std::string& checkpoint_name)
{
    assert(_blocked);
    assert(_queue.empty());
    if (_parent->context_unblocked(this, checkpoint_name)) // may yield
    {
        _blocked = false;
    }
    // if false returned, we may be in different thread, different context. do not touch 'this'!
}


void context::enqueue(coroutine_weak_ptr c)
{
    assert(!_blocked);
    _queue.push(std::move(c));
//    std::cout << "CONTEXT=" << this << " enqueued: " << c->name() << std::endl;
}

void context::enqueue(std::list<coroutine_weak_ptr>& cs)
{
    assert(!_blocked);
    _queue.push(cs);
}


void context::run()
{
    __current_context = this;
    struct scope_exit { ~scope_exit() { __current_context = nullptr; } } exit;

    try
    {
    for(;;)
    {
        coroutine_weak_ptr current_coro;
        bool has_next = _queue.pop(current_coro);

        // run the coroutine
        if (has_next)
        {
            CORO_LOG("CONTEXT=", this, " will run: ", current_coro->name());
            current_coro->run();
            if (_blocked)
                break;
        }
        else
        {
            std::list<coroutine_weak_ptr> stolen;
            _parent->steal(stolen);
            if (stolen.empty())
                break;
            _queue.push(stolen);
        }

        std::list<coroutine_weak_ptr> globals;
        _parent->get_all_from_global_queue(globals);
        _queue.push(globals);
    }
    } catch(const std::exception& e)
    {
        std::cerr << "UNEXPECTED EXCEPTION IN CONTEXT:::: " << e.what() << std::endl;
        std::terminate();
    }
    CORO_LOG("CONTEXT=", this, " finished ");
    _parent->context_finished(this);
}


unsigned context::steal(std::list<coroutine*>& out)
{
    unsigned result = 0;
    _queue.perform([&result, &out](std::list<coroutine_weak_ptr>& queue)
    {
        unsigned all = queue.size();
        result = all/2;

        auto it = queue.begin();
        std::advance(it, all-result);
        out.splice(out.end(), queue, it, queue.end());
    });

    return result;
}


}
