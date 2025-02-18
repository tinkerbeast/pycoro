#include <Python.h>

#include <atomic>
#include <deque>
#include <functional>

#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <ucontext.h>

#include <unistd.h> // TODO: remove this. Needed for sleep

// TODO: remove this
#if 0
#include <cstdio>
  #define PRINTX(...) printf(__VA_ARGS__)
#else
  #define PRINTX(...)
#endif

typedef void* (*coro_callable_f)(uint64_t);

struct CoroContext {
    bool is_new;
    uint64_t id;
    PyObject* callable;
    PyObject* args;
    PyObject* kwargs;
    ucontext_t ctx;
    PyThreadState* tsp;
};


void event_loop_entry_();

struct EventLoop { // TODO: per thread instance? or guarantee only one thread?

    bool stop_ = false; // TODO: atomic
    uint64_t max_coro_ = 0;
    uint64_t current_coro_ = -1;
    ucontext_t loop_ctx_;
    std::deque<uint64_t> q_; // TODO: threadsafe
    std::unordered_map<uint64_t, CoroContext> coros_; // TODO: splay tree based map would be better
    //PyThreadState tstate_;

    EventLoop() {
        memset(&loop_ctx_, 0, sizeof(loop_ctx_));
        //memset(&tstate_, 0, sizeof(tstate_));
    }

    ~EventLoop() = default;

    EventLoop(const EventLoop&) = delete;

    EventLoop(EventLoop&&) = delete;

    EventLoop& operator=(const EventLoop&) = delete;

    EventLoop& operator=(EventLoop&&) = delete;

    static EventLoop& get_event_loop();

    void stop() noexcept { stop_ = true; }

    void run_forever() noexcept {
        // TODO: NULL check for all PyGILState_GetThisThreadState
        PyThreadState* tstate_init = PyThreadState_Get();
        printf("Loop init default=%p\n", tstate_init);
        PyInterpreterState* interp = PyThreadState_GetInterpreter(tstate_init);
        printf("Loop init interp=%p\n", interp);
        while (!stop_) {
            // Get coro from scheduling queue.
            if (q_.empty()) {
                PRINTX("Sleeping\n");
                sleep(1);
                printf("Exiting\n");
                exit(EXIT_SUCCESS);
                continue;
            }
            uint64_t coro_id = q_.front();
            PRINTX("Loop1 %lu %lu\n", current_coro_, coro_id);
            q_.pop_front();
            CoroContext& c = coros_.at(coro_id);
            // Initialise if first time coro.
            if (c.is_new) {
                c.is_new = false;
                c.tsp = PyThreadState_New(interp);
                if (getcontext(&c.ctx) == -1) {
                    // TODO: error logging
                    std::abort();
                }
                c.ctx.uc_stack.ss_sp = new std::byte[4096]; // TODO: mmap is a better choice here
                c.ctx.uc_stack.ss_size = 4096;
                c.ctx.uc_link = &loop_ctx_;
                makecontext(&c.ctx, event_loop_entry_, 0);
            }
            // Context restore the Python thread state.
            PRINTX("Loop2 %lu %lu\n", current_coro_, coro_id);
            printf("Loop context switching_to=%p\n", c.tsp);
            PyThreadState_Swap(c.tsp);
            // Context restore the stack state.
            current_coro_ = coro_id;
            PRINTX("Loop3 %lu %lu\n", current_coro_, coro_id);
            if (swapcontext(&loop_ctx_, &c.ctx) == -1) {
                // TODO: error logging
                std::abort();
            }
            PRINTX("Loop4 %lu %lu\n", current_coro_, coro_id);
            printf("Loop loop switching_to=%p\n", tstate_init);
            PyThreadState* tfrom = PyThreadState_Swap(tstate_init);
            printf("Loop loop switching_from=%p\n", tfrom);
        }
    }

    void save_python_context_(PyThreadState* to, PyThreadState* from) {
        to->cframe = from->cframe;
        to->current_exception = from->current_exception;
        to->exc_info = from->exc_info;
    }

    void yield() {
        PRINTX("Yielding %lu\n", current_coro_);
        q_.push_back(current_coro_);
        CoroContext& c = coros_.at(current_coro_);
        // Save python context
        //c.tsp = PyEval_SaveThread();
        //printf("Yield saving tsp=%p\n", c.tsp);
        // Reset loop context
        current_coro_ = -1;
        // Switch thread context
        if (swapcontext(&c.ctx, &loop_ctx_) == -1) {
            // TODO: error logging
            std::abort();
        }
        // Restore python context
        //printf("Yield resoring from=%p\n", c.tsp);
        //PyThreadState* tfrom = PyThreadState_Swap(c.tsp);
        //printf("Yield resoring to=%p\n", tfrom);
    }

    void call_later(PyObject *func, PyObject *func_args, PyObject *func_kwargs) noexcept {
        // TODO: thread safety
        CoroContext c = {
            .is_new=true,
            .id=max_coro_,
            .callable=func,
            .args=func_args,
            .kwargs=func_kwargs
        };
        coros_.emplace(max_coro_, std::move(c));
        q_.push_back(max_coro_);
        ++max_coro_;
    }

};


EventLoop& EventLoop::get_event_loop() {
    static EventLoop instance; // TODO: could be thread local based on threading
    return instance;
}


void event_loop_entry_() {
    EventLoop& e = EventLoop::get_event_loop();
    CoroContext& c = e.coros_.at(e.current_coro_);
    PRINTX("Entering %lu\n", e.current_coro_);
    try {
        // TODO: handle multiple args
        // TODO: deal with return
        PyObject* ret = PyObject_CallNoArgs(c.callable);
    } catch (...) {
        // TODO: do something with the exception ptr
        // NOTE: **THIS MECHANISM CAN BE USE TO UNWIND THE THREAD AND KILL A CORO**
    }
    PRINTX("Exiting %p %lu\n", ret, e.current_coro_);
    e.coros_.erase(e.current_coro_);
    e.current_coro_ = -1;
}

#if 0
void* l3_func(uint64_t i) {
    EventLoop& e = EventLoop::get_event_loop();
    PRINTX("Entering l3 %lu %lu\n", e.current_coro_, i);
    if (std::rand() % 2 == 0) { e.yield(); }
    PRINTX("Returning l3 %lu %lu\n", e.current_coro_, i);
    return NULL;
}
void* l2_func(uint64_t i) {
    EventLoop& e = EventLoop::get_event_loop();
    PRINTX("Entering l2 %lu %lu\n", e.current_coro_, i);
    if (std::rand() % 2 == 0) { e.yield(); }
    l3_func(i);
    PRINTX("Returning l2 %lu %lu\n", e.current_coro_, i);
    return NULL;
}
void* l1_func(uint64_t i) {
    EventLoop& e = EventLoop::get_event_loop();
    PRINTX("Entering l1 %lu %lu\n", e.current_coro_, i);
    if (std::rand() % 2 == 0) { e.yield(); }
    l2_func(i);
    PRINTX("Returning l1 %lu %lu\n", e.current_coro_, i);
    return NULL;
}


int main() {

    auto& e = EventLoop::get_event_loop();

    e.call_later(l1_func);
    e.call_later(l1_func);
    e.call_later(l1_func);

    e.run_forever();

    return EXIT_SUCCESS;
}
#endif

static PyObject* foo(PyObject* self) {
    return PyUnicode_FromString("bar");
}


static PyObject* _call_later(PyObject* self, PyObject* args, PyObject* kwargs) {
    Py_ssize_t args_len = PyTuple_GET_SIZE(args);
    // TODO: what if callable is not passed? Or is not a callable object?
    PyObject* callable = PyTuple_GET_ITEM(args, 0);
    PyObject* call_args = args_len >= 2 ? PyTuple_GET_ITEM(args, 0) : NULL;
    PyObject* call_kwargs = args_len >= 3 ? PyTuple_GET_ITEM(args, 0) : NULL;

    auto& e = EventLoop::get_event_loop();
    e.call_later(callable, call_args, call_kwargs);

    // TODO: return PyNone with reference count
    return PyUnicode_FromString("_call_later");
}

static PyObject* _yield(PyObject* self) {
    EventLoop::get_event_loop().yield();
    // TODO: return PyNone with reference count
    return PyUnicode_FromString("_");
}

static PyObject* _run_forever(PyObject* self) {
    PyGILState_STATE gil_state = PyGILState_Ensure();
    EventLoop::get_event_loop().run_forever();
    PyGILState_Release(gil_state);
    // TODO: return PyNone with reference count
    return PyUnicode_FromString("_");
}

static PyObject* _current_coro(PyObject* self) {
    return PyLong_FromUnsignedLong(EventLoop::get_event_loop().current_coro_);
}

static PyMethodDef methods[] = {
    {"foo", (PyCFunction)foo, METH_NOARGS, NULL},
    {"call_later", (PyCFunction)_call_later, METH_VARARGS | METH_KEYWORDS, NULL},
    {"xyield", (PyCFunction)_yield, METH_NOARGS, NULL},
    {"run_forever", (PyCFunction)_run_forever, METH_NOARGS, NULL},
    {"current_coro", (PyCFunction)_current_coro, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef mod = {
    PyModuleDef_HEAD_INIT,
    "pycoro",
    NULL,
    -1,
    methods,
};

PyMODINIT_FUNC PyInit_pycoro(void)
{
    return PyModule_Create(&mod);
}
